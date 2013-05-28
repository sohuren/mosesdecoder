/***********************************************************************
 Moses - statistical machine translation system
 Copyright (C) 2006-2011 University of Edinburgh
 
 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 ***********************************************************************/

#pragma once

#include "Trie.h"
#include "moses/TranslationModel/PhraseDictionary.h"
#include "moses/InputType.h"
#include "moses/NonTerminal.h"
#include "moses/TranslationModel/fuzzy-match/FuzzyMatchWrapper.h"
#include "moses/TranslationModel/PhraseDictionaryNodeMemory.h"
#include "moses/TranslationModel/PhraseDictionaryMemory.h"

namespace Moses
{
  class PhraseDictionaryNodeMemory;
  
  /** Implementation of a SCFG rule table in a trie.  Looking up a rule of
   * length n symbols requires n look-ups to find the TargetPhraseCollection.
   */
  class PhraseDictionaryFuzzyMatch : public PhraseDictionary
  {
    friend std::ostream& operator<<(std::ostream&, const PhraseDictionaryFuzzyMatch&);
    friend class RuleTableLoader;
    
  public:
    PhraseDictionaryFuzzyMatch(const std::string &line);
    bool Load(const std::vector<FactorType> &input
              , const std::vector<FactorType> &output
              , const std::string &initStr
              , size_t tableLimit);
    
    const PhraseDictionaryNodeMemory &GetRootNode(const InputType &source) const;
    
    ChartRuleLookupManager *CreateRuleLookupManager(
                                                    const InputType &,
                                                    const ChartCellCollectionBase &);
    void InitializeForInput(InputType const& inputSentence);
    void CleanUpAfterSentenceProcessing(const InputType& source);
    
    virtual const TargetPhraseCollection *GetTargetPhraseCollection(const Phrase& src) const
    {
      assert(false);
      return NULL;
    }
    
    TO_STRING();
    
  protected:
    TargetPhraseCollection &GetOrCreateTargetPhraseCollection(PhraseDictionaryNodeMemory &rootNode
                                                              , const Phrase &source
                                                              , const TargetPhrase &target
                                                              , const Word *sourceLHS);
    
    PhraseDictionaryNodeMemory &GetOrCreateNode(PhraseDictionaryNodeMemory &rootNode
                                              , const Phrase &source
                                              , const TargetPhrase &target
                                              , const Word *sourceLHS);
    
    void SortAndPrune(PhraseDictionaryNodeMemory &rootNode);
    PhraseDictionaryNodeMemory &GetRootNode(const InputType &source);

    std::map<long, PhraseDictionaryNodeMemory> m_collection;
    std::vector<std::string> m_config;
    
    const std::vector<FactorType> *m_input, *m_output;
    const std::vector<float> *m_weight;
    
    tmmt::FuzzyMatchWrapper *m_FuzzyMatchWrapper;

  };
  
}  // namespace Moses
