/*

    This file is part of the Maude 2 interpreter.

    Copyright 1997-2003 SRI International, Menlo Park, CA 94025, USA.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.

*/

#include <sys/time.h>

//      utility stuff
#include "macros.hh"
#include "vector.hh"
#include "pointerSet.hh"

#include "runtime.hh"
#define CHUNK_SIZE (1024 * 1024)
#define E_SIZE (1024 * 1024)
#define FACTOR 4

//template class Vector<void*>;

extern int arity[];

void outputGraph(Node* node);
void depthFirstTraversal(Node* node, PointerSet& visited);
Node* inputGraph();

Context g;

//
//	Current half space
//
Vector<void*> currentSpace;
Vector<void*> endMarker;  // for breadth-first copying
int curChunk;
char* curNext;
char* curEnd;

//
//	Next half space
//
Vector<void*> newSpace;
//
//	Ephemeral space
//
Node* ephemeralStart;
Node* ephemeralEnd;

static inline Node*
evacuate(Node*& old, char*& nextFree, char*& endFree)
  //
  //	Copy node to current space.
  //
{
  int flags = old->flags;
  if (flags & EVACUATED)
    return old->fwd;  // forwarding pointer
  int symbol = old->symbol;
  int nrArgs = arity[symbol];
  if (flags & UNREDUCED)
    ++nrArgs;
  int nrBytes = sizeof(Node) + sizeof(Node*) * nrArgs;
  char* t = nextFree + nrBytes;
  if (t > endFree)
    {
      endMarker[curChunk] = reinterpret_cast<void*>(nextFree);  // mark end of used portion
      ++curChunk;
      nextFree = reinterpret_cast<char*>(currentSpace[curChunk]);
      endFree = nextFree + CHUNK_SIZE;
      t = nextFree + nrBytes;
    }
  Node* n = reinterpret_cast<Node*>(nextFree);
  nextFree = t;
  n->symbol = symbol;
  n->flags = flags;
  n->sortIndex = old->sortIndex;
  for (int i = 0; i < nrArgs; i++)
    n->args[i] = old->args[i];
  old->flags = flags | EVACUATED;
  old->fwd = n;
  return n;
}

void ephemeralGC();
void fullGC();

void
collectGarbage()
{
  if (curChunk + 1 < currentSpace.length())
    ephemeralGC();
  else
    fullGC();
  //
  //	reuse ephemeral space.
  //
  g.memNext = reinterpret_cast<char*>(ephemeralStart);
}

void 
ephemeralGC()
{  
  //
  //	keep track of where we start putting copies.
  //
  int copyStartChunk = curChunk;
  char* copyStart = curNext;
  char* cn = curNext;
  char* ce = curEnd;
  Node* es = ephemeralStart;
  Node* ee = ephemeralEnd;

  //
  //	scan arg list for pointers into ephemeral space.
  //
  int nrArgs = g.nrArgs;
  for (int i = 0; i < nrArgs; i++)
    {
      Node* t = g.args[i];
      if (t >= es && t < ee)
	g.args[i] = evacuate(t, cn, ce);
    }

  //
  //	scan stack for pointers into ephemeral space.
  //
  for (Link* s = g.safePtr; s != 0; s = s[0].l)
    {
      int nrSlots = s[1].i;
      for (int i = 2; i < nrSlots + 2; i++)
	{
	  Node* t = s[i].n;
	  if (t != 0 && t >= es && t < ee)
	    s[i].n = evacuate(t, cn, ce);
	}
    }

  //
  //	breadth-first scan of copies for pointers into ephemeral space.
  //
  for (int i = copyStartChunk; i <= curChunk; i++)
    {
      char* p = static_cast<char*>((i == copyStartChunk) ? copyStart : currentSpace[i]);
      for (;;)
	{
	  if (i == curChunk)
	    {
	      if (p == curNext)
		break;
	    }
	  else
	    {
	      if (p == endMarker[i])
		break;
	    }
	  Node* n = reinterpret_cast<Node*>(p);
	  int nrArgs = arity[n->symbol];
	  p += sizeof(Node) + sizeof(Node*) * nrArgs;
	  if (n->flags & UNREDUCED)
	    p += sizeof(Node*);
	  for (int j = 0; j < nrArgs; j++)
	    {
	      Node* t = n->args[j];
	      if (t >= es && t < ee)
		n->args[j] = evacuate(t, cn, ce);
	    }
	}
    }
  //
  //	Update global pointers.
  //
  curNext = cn;
  curEnd = ce;
}

void 
fullGC()
{
  //
  //	Set up new current space.
  //
  currentSpace.swap(newSpace);
  curChunk = 0;
  char* cn = reinterpret_cast<char*>(currentSpace[0]);;
  char* ce = cn + CHUNK_SIZE;

  //
  //	scan arg list for pointers.
  //
  int nrArgs = g.nrArgs;
  for (int i = 0; i < nrArgs; i++)
    g.args[i] = evacuate(g.args[i], cn, ce);
  
  //
  //	scan stack for pointers.
  //
  for (Link* s = g.safePtr; s != 0; s = s[0].l)
    {
      int nrSlots = s[1].i;
      for (int i = 2; i < nrSlots + 2; i++)
	{
	  Node* t = s[i].n;
	  if (t != 0)
	    s[i].n = evacuate(t, cn, ce);
	}
    }

  //
  //	breadth-first scan of copies for pointers.
  //
  for (int i = 0; i <= curChunk; i++)
    {
      for (char* p = reinterpret_cast<char*>(currentSpace[i]);;)
	{
	  if (i == curChunk)
	    {
	      if (p == curNext)
		break;
	    }
	  else
	    {
	      if (p == endMarker[i])
		break;
	    }
	  Node* n = reinterpret_cast<Node*>(p);
	  int nrArgs = arity[n->symbol];
	  p += sizeof(Node) + sizeof(Node*) * nrArgs;
	  if (n->flags & UNREDUCED)
	    p += sizeof(Node*);
	  for (int j = 0; j < nrArgs; j++)
	    n->args[j] = evacuate(n->args[j], cn, ce);
	}
    }
  //
  //	Update global pointers.
  //
  curNext = cn;
  curEnd = ce;
  //
  //	Allocate more space if needed.
  //
  int used = (curChunk + 1) * CHUNK_SIZE - (curEnd - curNext);
  int allocated = newSpace.length() * CHUNK_SIZE;
  int needed = FACTOR * used;
  while (needed > allocated)
    {
      currentSpace.append(malloc(CHUNK_SIZE));
      newSpace.append(malloc(CHUNK_SIZE));
      void* t = 0;
      endMarker.append(t);
      allocated += CHUNK_SIZE;
    }
  
  //
  //	Keep length newSpace = length currentSpace + 1
  //
  int t = currentSpace.length() - 1;
  newSpace.append(currentSpace[t]);
  currentSpace.contractTo(t);
}

static itimerval init = { {1000000, 999999}, {1000000, 999999} };
static itimerval result;
static itimerval result2;

void
initMem()
{
  ephemeralStart = static_cast<Node*>(malloc(E_SIZE));
  ephemeralEnd = ephemeralStart + E_SIZE / sizeof(Node);

  currentSpace.append(malloc(CHUNK_SIZE));
  currentSpace.append(malloc(CHUNK_SIZE));
  void* t = 0;
  endMarker.append(t);
  endMarker.append(t);
  curChunk = 1;
  curNext = static_cast<char*>(currentSpace[0]);
  curEnd = curNext + CHUNK_SIZE;

  newSpace.append(malloc(CHUNK_SIZE));
  newSpace.append(malloc(CHUNK_SIZE));
  newSpace.append(malloc(CHUNK_SIZE));

  g.safePtr = 0;
  g.count = 0;
  g.memNext = reinterpret_cast<char*>(ephemeralStart);
  g.memEnd = reinterpret_cast<char*>(ephemeralEnd);
}

int
main(/* int argc, char* argv[] */)
{
  initMem();
  Node* n = inputGraph();
  
  setitimer(ITIMER_REAL, &init, 0);
  setitimer(ITIMER_PROF, &init, 0);

  n = eval(n);

  getitimer(ITIMER_PROF, &result);
  getitimer(ITIMER_REAL, &result2);

  outputGraph(n);

  /*
  //outputGraph(inputGraph());
  outputGraph(eval(inputGraph()));

  for (;;)
    {
      Node* n = inputGraph();
      //      if (n == 0)
      //	break;
      n = eval(n);
      outputGraph(n);
    }
  */
}

void
outputGraph(Node* node)
{
  ofstream ofile("inGraph");
  double cpu = (init.it_value.tv_sec - result.it_value.tv_sec) +
    (init.it_value.tv_usec - result.it_value.tv_usec) / 1000000.0;
  double real = (init.it_value.tv_sec - result2.it_value.tv_sec) +
    (init.it_value.tv_usec - result2.it_value.tv_usec) / 1000000.0;

  ofile << g.count << ' ' << int(cpu * 1000) << ' ' << int(real * 1000) << '\n';
  PointerSet visited;
  depthFirstTraversal(node, visited);
  int nrNodes = visited.cardinality();
  for (int i = 0; i < nrNodes; i++)
    {
      Node* n = reinterpret_cast<Node*>(visited.index2Pointer(i));
      int symbol = n->symbol;
      ofile << symbol;
      int nrArgs = arity[symbol];
      for (int j = 0; j < nrArgs; j++)
	ofile << ' ' << visited.pointer2Index(n->args[j]);
      ofile << '\n';
    }
}

void
depthFirstTraversal(Node* node, PointerSet& visited)
{ 
  int nrArgs = arity[node->symbol];
  for (int i = 0; i < nrArgs; i++)
    {
      Node* n = node->args[i];
      if (!(visited.contains(n)))
        depthFirstTraversal(n, visited);
    }
  visited.insert(node);
}

Node*
inputGraph()
{
  ifstream ifile("outGraph");
  Vector<void*> built;
  for(;;)
    {
      int symbol;
      if(!(ifile >> symbol))
	break;
      int nrArgs = arity[symbol];
      // cerr << "symbol = " << symbol << "  arity = " << nrArgs << '\n';
      Node* r = reinterpret_cast<Node*>(g.memNext);
      char* n = g.memNext + sizeof(Node) + (nrArgs + 1) * sizeof(Node*);
      if (n > g.memEnd)
	{
	  collectGarbage();
	  r = reinterpret_cast<Node*>(g.memNext);
	  g.memNext += sizeof(Node) + (nrArgs + 1) * sizeof(Node*);
	}
      else
	g.memNext = n;
      r->symbol = symbol;
      r->flags = UNREDUCED;
      for (int i = 0; i < nrArgs; i++)
	{
	  int t;
	  ifile >> t;
	  r->args[i] = reinterpret_cast<Node*>(built[t]);
	}
      built.append(r);
    }
  return reinterpret_cast<Node*>(built[built.length() - 1]);
}
