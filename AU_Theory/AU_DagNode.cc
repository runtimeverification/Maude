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

//
//      Implementation for class AU_DagNode.
//
 
//	utility stuff
#include "macros.hh"
#include "vector.hh"
 
//      forward declarations
#include "interface.hh"
#include "core.hh"
#include "AU_Theory.hh"
 
//      interface class definitions
#include "term.hh"

//      AU theory class definitions
#include "AU_Symbol.hh"
#include "AU_DagNode.hh"
#include "AU_DequeDagNode.hh"
#include "AU_DagArgumentIterator.hh"
#include "AU_ExtensionInfo.hh"
#include "AU_Subproblem.hh"

//	our stuff
#include "AU_Normalize.cc"
#include "AU_DagOperations.cc"

AU_DagNode*
getAU_DagNode(DagNode* d)
{
  if (safeCast(AU_BaseDagNode*, d)->isDeque())
    return AU_DequeDagNode::dequeToArgVec(safeCast(AU_DequeDagNode*, d));
  return safeCast(AU_DagNode*, d);
}

RawDagArgumentIterator*
AU_DagNode::arguments()
{
  return new AU_DagArgumentIterator(argArray);
}

size_t
AU_DagNode::getHashValue()
{
  size_t hashValue = symbol()->getHashValue();
  FOR_EACH_CONST(i, ArgVec<DagNode*>, argArray)
    hashValue = hash(hashValue, (*i)->getHashValue());
  return hashValue;
}

int
AU_DagNode::compareArguments(const DagNode* other) const
{
  if (safeCast(const AU_BaseDagNode*, other)->isDeque())
    return - safeCast(const AU_DequeDagNode*, other)->compare(this);

  const ArgVec<DagNode*>& argArray2 = safeCast(const AU_DagNode*, other)->argArray;
  int r = argArray.length() - argArray2.length();
  if (r != 0)
    return r;
 
  ArgVec<DagNode*>::const_iterator j = argArray2.begin();
  FOR_EACH_CONST(i, ArgVec<DagNode*>, argArray)
    {
      int r = (*i)->compare(*j);
      if (r != 0)
	return r;
      ++j;
    }
  Assert(j == argArray2.end(), "iterator problem");

  return 0;
}

DagNode*
AU_DagNode::markArguments()
{
  Assert(argArray.length() > 0, "no arguments");
  argArray.evacuate();
  //
  //	We avoid recursing on the first subterm that shares our symbol.
  //
  Symbol* s = symbol();
  DagNode* r = 0;
  FOR_EACH_CONST(i, ArgVec<DagNode*>, argArray)
    {
      DagNode* d = *i;
      if (r == 0 && d->symbol() == s)
	r = d;
      else
	d->mark();
    }
  return r;
}

DagNode*
AU_DagNode::copyEagerUptoReduced2()
{
  int nrArgs = argArray.length();
  AU_Symbol* s = safeCast(AU_Symbol*, symbol());
  AU_DagNode* n = new AU_DagNode(s, nrArgs);
  if (s->getPermuteStrategy() == BinarySymbol::EAGER)
    {
      for (int i = 0; i < nrArgs; i++)
	  n->argArray[i] = argArray[i]->copyEagerUptoReduced();
    }
  else
    copy(argArray.begin(), argArray.end(), n->argArray.begin());
  return n;
}

void
AU_DagNode::clearCopyPointers2()
{
  FOR_EACH_CONST(i, ArgVec<DagNode*>, argArray)
    (*i)->clearCopyPointers();
}

void
AU_DagNode::overwriteWithClone(DagNode* old)
{
  AU_DagNode* d = new(old) AU_DagNode(symbol(), argArray.length());
  d->copySetRewritingFlags(this);
  d->setTheoryByte(getTheoryByte());
  d->setSortIndex(getSortIndex());
  copy(argArray.begin(), argArray.end(), d->argArray.begin());
}

DagNode*
AU_DagNode::makeClone()
{
  int nrArgs = argArray.length();
  AU_DagNode* d = new AU_DagNode(symbol(), nrArgs);
  d->copySetRewritingFlags(this);
  d->setTheoryByte(getTheoryByte());
  d->setSortIndex(getSortIndex());
  copy(argArray.begin(), argArray.end(), d->argArray.begin());
  return d;
}

DagNode*
AU_DagNode::copyWithReplacement(int argIndex, DagNode* replacement)
{
  int nrArgs = argArray.length();
  AU_DagNode* n = new AU_DagNode(symbol(), nrArgs);
  ArgVec<DagNode*>& args2 = n->argArray;
  for (int i = 0; i < nrArgs; i++)
    args2[i] = (i == argIndex) ? replacement : argArray[i];
  return n;
}

DagNode*
AU_DagNode::copyWithReplacement(Vector<RedexPosition>& redexStack,
				int first,
				int last)
{
  int nrArgs = argArray.length();
  AU_DagNode* n = new AU_DagNode(symbol(), nrArgs);
  ArgVec<DagNode*>& args = n->argArray;
  int nextReplacementIndex = redexStack[first].argIndex();
  for (int i = 0; i < nrArgs; i++)
    {
      if (i == nextReplacementIndex)
	{
	  args[i] = redexStack[first].node();
	  ++first;
	  nextReplacementIndex = (first <= last) ?
	    redexStack[first].argIndex() : NONE;
	}
      else
	args[i] = argArray[i];
    }
  return n;
}

void
AU_DagNode::stackArguments(Vector<RedexPosition>& stack,
			   int parentIndex,
			   bool respectFrozen)
{
  if (respectFrozen && !(symbol()->getFrozen().empty()))
    return;
  int nrArgs = argArray.length();
  for (int i = 0; i < nrArgs; i++)
    {
      DagNode* d = argArray[i];
      if (!(d->isUnstackable()))
	stack.append(RedexPosition(d, parentIndex, i));
    }
}

void
AU_DagNode::partialReplace(DagNode* replacement, ExtensionInfo* extensionInfo)
{
  AU_ExtensionInfo* e = safeCast(AU_ExtensionInfo*, extensionInfo);
  int first = e->firstMatched();
  int last = e->lastMatched();
  argArray[first++] = replacement;
  int nrArgs = argArray.length();
  for (last++; last < nrArgs; last++)
    argArray[first++] = argArray[last];
  argArray.contractTo(first);
  repudiateSortInfo();
}

DagNode*
AU_DagNode::partialConstruct(DagNode* replacement, ExtensionInfo* extensionInfo)
{
  AU_ExtensionInfo* e = safeCast(AU_ExtensionInfo*, extensionInfo);
  int first = e->firstMatched();
  int last = e->lastMatched();
  int nrArgs = argArray.length();
  AU_DagNode* n = new AU_DagNode(symbol(), nrArgs + first - last);
  ArgVec<DagNode*>& args2 = n->argArray;
  for (int i = 0; i < first; i++)
    args2[i] = argArray[i]; 
  args2[first++] = replacement;
  for (last++; last < nrArgs; last++)
    args2[first++] = argArray[last]; 
  return n;
}

ExtensionInfo*
AU_DagNode::makeExtensionInfo()
{
  return new AU_ExtensionInfo(this);
}

bool
AU_DagNode::matchVariableWithExtension(int index,
				      const Sort* sort,
				      Substitution& /* solution */,
				      Subproblem*& returnedSubproblem,
				      ExtensionInfo* extensionInfo)
{
  //
  //    This code could be much more sophisticated: in particular we could look for
  //    the variable having too smaller sort and return false; the subject having
  //    total subterm multiplicity of 2 and return unique solution.
  //
  AU_ExtensionInfo* e = safeCast(AU_ExtensionInfo*, extensionInfo);
  AU_Subproblem* subproblem = new AU_Subproblem(this, 0, argArray.length() - 1, 1, e);
  int min = symbol()->oneSidedId() ? 1 : 2;
  subproblem->addTopVariable(0, index, min, UNBOUNDED, const_cast<Sort*>(sort));  // HACK
  subproblem->complete();
  returnedSubproblem = subproblem;
  extensionInfo->setValidAfterMatch(false);
  return true;
}
