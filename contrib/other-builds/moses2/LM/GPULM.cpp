/*
 * GPULM.cpp
 *
 *  Created on: 4 Nov 2015
 *      Author: hieu
 */
#include <boost/foreach.hpp>
#include <sstream>
#include <vector>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include "GPULM.h"
#include "../Phrase.h"
#include "../Scores.h"
#include "../System.h"
#include "../PhraseBased/Hypothesis.h"
#include "../PhraseBased/Manager.h"
#include "util/exception.hh"
#include "../legacy/FactorCollection.h"
#include <boost/thread/tss.hpp>

using namespace std;

namespace Moses2
{

struct GPULMState: public FFState
{
  virtual std::string ToString() const
  {
    return "GPULMState";
  }

  virtual size_t hash() const
  {
    size_t ret = boost::hash_value(lastWords);
    cerr << "  hash=" << ret << endl;
    return ret;
  }

  virtual bool operator==(const FFState& other) const
  {
    const GPULMState &otherCast = static_cast<const GPULMState&>(other);
    bool ret = lastWords == otherCast.lastWords;
    cerr << "  ret=" << ret << endl;

    return ret;
  }

  void SetContext(const Context &context)
  {
    lastWords = context;
    if (lastWords.size()) {
      lastWords.resize(lastWords.size() - 1);
    }
  }

  Context lastWords;
};


/////////////////////////////////////////////////////////////////
GPULM::GPULM(size_t startInd, const std::string &line)
:StatefulFeatureFunction(startInd, line)
{
  cerr << "GPULM::GPULM" << endl;
  ReadParameters();
}

GPULM::~GPULM()
{
  // Free pinned memory:
  //@TODO FREE PINNED MEMORY HERE
  //pinnedMemoryDeallocator(ngrams_for_query);
  //pinnedMemoryDeallocator(results);
}

float * GPULM::getThreadLocalResults() const {
    float * res = m_results.get();
    if(!res) {
        float * local_results = new float[max_num_queries];
        //pinnedMemoryAllocator(local_results, max_num_queries); //Max max_num_queries ngram batches @TODO NO FREE
        m_results.reset(local_results);
        res = local_results;
    }
    return res;
}

unsigned int * GPULM::getThreadLocalngrams() const {
    unsigned int * res = m_ngrams_for_query.get();
    if(!res) {
    
        unsigned int * ngrams_for_query = new unsigned int[max_num_queries*max_ngram_order];
        //pinnedMemoryAllocator(ngrams_for_query, max_num_queries*max_ngram_order); //Max max_num_queries ngram batches @TODO NO FREEs
        m_ngrams_for_query.reset(ngrams_for_query);
        res = ngrams_for_query;
    }
    return res;
    
}

void GPULM::Load(System &system)
{
  int deviceID = 0; //@TODO This is an optional argument
  max_num_queries = 20000;
  m_obj = new gpuLM(m_path, max_num_queries, deviceID);
  cerr << "GPULM::Load" << endl;
  
  //Allocate host memory here. Size should be same as the constructor
  max_ngram_order = m_obj->getMaxNumNgrams();
  m_order = max_ngram_order;
  
  //Add factors 
  FactorCollection &vocab = system.GetVocab();
  std::unordered_map<std::string, unsigned int>& origmap = m_obj->getEncodeMap();

  for (auto it : origmap) {
    const Factor *factor = vocab.AddFactor(it.first, system, false);
    encode_map.insert(std::make_pair(factor, it.second));
  }
  
}

void GPULM::SetParameter(const std::string& key,
    const std::string& value)
{
  //cerr << "key=" << key << " " << value << endl;
  if (key == "path") {
    m_path = value;
  }
  else if (key == "order") {
    m_order = Scan<size_t>(value);
  }
  else if (key == "factor") {
    m_factorType = Scan<FactorType>(value);
  }
  else {
    StatefulFeatureFunction::SetParameter(key, value);
  }

  //cerr << "SetParameter done" << endl;
}

FFState* GPULM::BlankState(MemPool &pool) const
{
  GPULMState *ret = new (pool.Allocate<GPULMState>()) GPULMState();
  return ret;
}

//! return the state associated with the empty hypothesis for a given sentence
void GPULM::EmptyHypothesisState(FFState &state, const ManagerBase &mgr,
    const InputType &input, const Hypothesis &hypo) const
{
  GPULMState &stateCast = static_cast<GPULMState&>(state);
  stateCast.lastWords.push_back(m_bos);
}

void GPULM::EvaluateInIsolation(MemPool &pool, const System &system,
    const Phrase<Moses2::Word> &source, const TargetPhrase<Moses2::Word> &targetPhrase, Scores &scores,
    SCORE *estimatedScore) const
{/*
  if (targetPhrase.GetSize() == 0) {
    return;
  }

  SCORE score = 0;
  SCORE nonFullScore = 0;
  Context context;
//  context.push_back(m_bos);

  context.reserve(m_order);
  for (size_t i = 0; i < targetPhrase.GetSize(); ++i) {
    const Factor *factor = targetPhrase[i][m_factorType];
    ShiftOrPush(context, factor);

    unsigned int position = 0; //Position in ngrams_for_query array
    unsigned int num_queries = 0;
    CreateQueryVec(context, position);

    if (context.size() == m_order) {
      //std::pair<SCORE, void*> fromScoring = Score(context);
      //score += fromScoring.first;
    }
    else if (estimatedScore) {
      //std::pair<SCORE, void*> fromScoring = Score(context);
      //nonFullScore += fromScoring.first;
    }
  }*/

}

void GPULM::EvaluateInIsolation(MemPool &pool, const System &system, const Phrase<SCFG::Word> &source,
    const TargetPhrase<SCFG::Word> &targetPhrase, Scores &scores,
    SCORE *estimatedScore) const
{
  UTIL_THROW2("Not implemented");
}

void GPULM::EvaluateWhenApplied(const ManagerBase &mgr,
    const Hypothesis &hypo, const FFState &prevState, Scores &scores,
    FFState &state) const
{
  UTIL_THROW2("Not implemented");
}

void GPULM::EvaluateWhenAppliedBatch(
    const System &system,
    const Batch &batch) const
{
  // create list of ngrams
  std::vector<std::pair<Hypothesis*, Context> > contexts;

  cerr << "NB1" << endl;
  for (size_t i = 0; i < batch.size(); ++i) {
    Hypothesis *hypo = batch[i];
    CreateNGram(contexts, *hypo);
  }
  cerr << "NB2" << endl;
  
  unsigned int * ngrams_for_query = getThreadLocalngrams();
  cerr << "NB3" << endl;
  float * results = getThreadLocalResults();
  cerr << "NB4" << endl;
  
  //Create the query vector
  unsigned int position = 0; //Position in ngrams_for_query array
  unsigned int num_queries = 0;
  for (auto context : contexts) {
    num_queries++;
    CreateQueryVec(context.second, position);
  }
  cerr << "NB5" << endl;

  //Score here + copy back-and-forth
  m_obj->query(results, ngrams_for_query, num_queries);
  cerr << "NB6" << endl;

  // score ngrams
  for (size_t i = 0; i < contexts.size(); ++i) {
    const Context &context = contexts[i].second;
    Hypothesis *hypo = contexts[i].first;
    SCORE score = results[i];
    Scores &scores = hypo->GetScores();
    
    cerr << i << " " << &hypo << " " << &scores << " " << results[i] << endl;

    scores.PlusEquals(system, *this, score);
  }
  cerr << "NB7" << endl;
}

void GPULM::CreateQueryVec(
		  const Context &context,
		  unsigned int &position) const
{
    unsigned int * ngrams_for_query = getThreadLocalngrams();
    int counter = 0; //Check for non full ngrams

    for (auto factor : context) {
      auto vocabID = encode_map.find(factor);
      if (vocabID == encode_map.end()){
        ngrams_for_query[position] = 1; //UNK
      } else {
        ngrams_for_query[position] = vocabID->second;
      }
      counter++;
      position++;
    }

    while (counter < max_ngram_order) {
      ngrams_for_query[position] = 0;
      counter++;
      position++;
    }
}

void GPULM::CreateNGram(std::vector<std::pair<Hypothesis*, Context> > &contexts, Hypothesis &hypo) const
{
  const TargetPhrase<Moses2::Word> &tp = hypo.GetTargetPhrase();

  if (tp.GetSize() == 0) {
    return;
  }

  const Hypothesis *prevHypo = hypo.GetPrevHypo();
  assert(prevHypo);
  const FFState *prevState = prevHypo->GetState(GetStatefulInd());
  assert(prevState);
  const GPULMState &prevStateCast = static_cast<const GPULMState&>(*prevState);

  Context context = prevStateCast.lastWords;
  context.reserve(m_order);

  for (size_t i = 0; i < tp.GetSize(); ++i) {
    const Word &word = tp[i];
    const Factor *factor = word[m_factorType];
    ShiftOrPush(context, factor);

    std::pair<Hypothesis*, Context> ele(&hypo, context);
    contexts.push_back(ele);
  }

  FFState *state = hypo.GetState(GetStatefulInd());
  GPULMState &stateCast = static_cast<GPULMState&>(*state);
  stateCast.SetContext(context);
}

void GPULM::ShiftOrPush(std::vector<const Factor*> &context,
    const Factor *factor) const
{
  if (context.size() < m_order) {
    context.resize(context.size() + 1);
  }
  assert(context.size());

  for (size_t i = context.size() - 1; i > 0; --i) {
    context[i] = context[i - 1];
  }

  context[0] = factor;
}

}
