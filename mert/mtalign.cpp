
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <iterator>

#include <boost/program_options.hpp>
#include <boost/scoped_ptr.hpp>

#include "util/exception.hh"
#include "util/string_piece.hh"
#include "util/string_piece_hash.hh"

#include "Scorer.h"
#include "ScorerFactory.h"

#include <boost/unordered_map.hpp>

using namespace MosesTuning;

namespace po = boost::program_options;

const size_t MAX_NGRAM_ORDER = 4;

StringPiece operator+(const StringPiece& s1, const StringPiece& s2) {
  const char* start = std::min(s1.data(), s2.data()); 
  const char* end   = std::max(s1.data() + s1.length(), s2.data() + s2.length());
  size_t length = end - start;
  return StringPiece(start, length);
}

typedef std::vector<StringPiece> NGramsByOrder;
typedef std::vector<NGramsByOrder> NGrams;    

class Sentence {
  public:
    Sentence(std::vector<StringPiece>& tokens)
    : m_sentence(StringPiece()), m_tokens(&tokens), m_start(0), m_length(0)
    {}
    
    Sentence(StringPiece sentence, size_t start, size_t length,
             std::vector<StringPiece>& tokens)
    : m_sentence(sentence), m_tokens(&tokens), m_start(start), m_length(length)
    {
      CollectNGrams();
    }
        
    StringPiece str() const {
      return m_sentence;
    }
    
    StringPiece operator[](size_t i) const {
      return (*m_tokens)[m_start + i];
    }
    
    size_t size() const {
      return m_length;
    }
    
    Sentence operator+(const Sentence& s) const {
      size_t start = std::min(m_start, s.m_start); 
      size_t end   = std::max(m_start + m_length, s.m_start + s.m_length);
      size_t length = end - start;
      return Sentence(m_sentence + s.m_sentence, start, length, *m_tokens);
    }
    
    void CollectNGrams() {
      if(m_ngrams.empty()) {
        m_ngrams.resize(MAX_NGRAM_ORDER);
        for(size_t i = 0; i < size(); i++) {
          for(size_t j = 0; j < MAX_NGRAM_ORDER && i + j < size(); j++) {
            StringPiece ngram = (*this)[i] + (*this)[i + j];
            m_ngrams[j].push_back(ngram);
          }
        }
        for(size_t i = 0; i < MAX_NGRAM_ORDER; i++)
          std::sort(m_ngrams[i].begin(), m_ngrams[i].end());
      }
    }
    
    const NGrams& ngrams() const {
      return m_ngrams;
    }
    
  private:
    StringPiece m_sentence;
    std::vector<StringPiece>* m_tokens;
    size_t m_start;
    size_t m_length;

    NGrams m_ngrams;
};

inline std::ostream& operator<<(std::ostream& o, const Sentence& sentence) {
  return o << sentence.str();
}

class Corpus {
  public:
    
    typedef std::pair<size_t, size_t> Range;
    typedef boost::unordered_map<Range, Sentence*> Ranges;
    
    Corpus(const std::string& fileName)
      : m_fileName(fileName), m_emptySentence(m_tokens) {
      std::ifstream fileStream(m_fileName.c_str());
      if (!fileStream.good())
        throw std::runtime_error("Error opening file:" + fileName);
      
      size_t currentPos = 0;
      std::vector<size_t> sentenceStarts;
      std::vector<size_t> tokenStarts;
      
      std::stringstream corpusStream;
      std::string line;
      while (std::getline(fileStream, line)) {
        sentenceStarts.push_back(currentPos);
        
        std::stringstream lineStream(line);
        std::string token;
        while(lineStream >> token) {
          tokenStarts.push_back(currentPos);
          corpusStream << token << " ";
          currentPos += token.size() + 1;
        }
      }
      
      m_corpus = corpusStream.str();
      
      size_t j = 0;
      for(size_t i = 0; i < sentenceStarts.size() - 1; i++) {
        size_t start = sentenceStarts[i];
        size_t length = sentenceStarts[i + 1] - start - 1;
        
        if(sentenceStarts[i] == sentenceStarts[i + 1])
          length = 0;
        
        size_t tokens = 0;
        while(tokenStarts[j + tokens] < start + length) {
          size_t tStart =  tokenStarts[j + tokens];
          size_t tLength = tokenStarts[j + tokens + 1] - tStart - 1;
          m_tokens.push_back(StringPiece(m_corpus.c_str() + tStart, tLength));
          tokens++;
        }
        
        Sentence sentence(StringPiece(m_corpus.c_str() + start, length),
                          j, tokens, m_tokens);
        m_sentences.push_back(sentence);
        
        j += tokens;
      }
      
      size_t start = sentenceStarts.back();
      size_t length = currentPos - start - 1;
      
      if(start == currentPos)
        length = 0;
      
      size_t tokens = 0;
      while(tokenStarts[j + tokens] < start + length && j + tokens < tokenStarts.size()) {
        size_t tStart =  tokenStarts[j + tokens];
        size_t tLength = tokenStarts[j + tokens + 1] - tStart - 1;
        
        if(j + tokens + 1 >= tokenStarts.size())
          tLength = currentPos - tStart - 1;
        
        m_tokens.push_back(StringPiece(m_corpus.c_str() + tStart, tLength));
        tokens++;
      }
      
      Sentence sentence(StringPiece(m_corpus.c_str() + start, length),
                        j, tokens, m_tokens);
      m_sentences.push_back(sentence);
    }
    
    const Sentence& operator()(size_t i, size_t j) {
      if(i == j) {
        return m_sentences[i];
      }
      else if(j > i) {
        Range range(i, j);
        Ranges::iterator it = m_ranges.find(range);
        if(it != m_ranges.end()) {
          return *it->second;
        }
        else {
          Sentence* sentenceRange = new Sentence(m_sentences[i] + m_sentences[j]);
          m_ranges[range] = sentenceRange;
          return *m_ranges[range];
        }
      }
      else {
        return m_emptySentence;
      }
    }
    
    const Sentence& operator[](size_t i) const {
      return m_sentences[i];
    }
    
    size_t size() const {
      return m_sentences.size();
    }
    
  private:
    std::string m_fileName;
    
    std::string m_corpus;
    std::vector<StringPiece> m_tokens;
    std::vector<Sentence> m_sentences;
    Sentence m_emptySentence;
    Ranges m_ranges;
};

class Stats {
  public:
    Stats()
    : m_stats(MAX_NGRAM_ORDER * 3, 0)
    {}
    
    float& operator[](size_t i) {
      return m_stats[i];
    }
    
    const float& operator[](size_t i) const {
      return m_stats[i];
    }
    
    float& back() {
      return m_stats.back();
    }
    
    size_t size() const {
      return m_stats.size();
    }
    
    Stats& operator+=(Stats& o) {
      for(size_t i = 0; i < m_stats.size(); i++)
        m_stats[i] += o[i];
      return *this;
    }
    
    Stats operator+(Stats o) {
      Stats out;
      out += o;
      out += *this;
      return out;
    }
    
  private:
    std::vector<float> m_stats;
};

inline std::ostream& operator<<(std::ostream& o, const Stats& stats) {
  for(size_t i = 0; i < stats.size(); i++) {
    o << const_cast<Stats&>(stats)[i];
    if(i < stats.size() - 1)
      o << " ";
  }
  return o;
}

void countCommon(const NGramsByOrder& n1, const NGramsByOrder& n2, size_t& common) {
  NGramsByOrder::const_iterator it1 = n1.begin();
  NGramsByOrder::const_iterator it2 = n2.begin();
  
  common = 0;
  while (it1 != n1.end() && it2 != n2.end()) {
    if (*it1 < *it2) {
      ++it1;
    }
    else {
      if (!(*it2 < *it1)) {
        ++common;
        ++it1;
      }
      ++it2;
    }
  }
}

void computeBLEU2stats(const Sentence& c, const Sentence& r, Stats& stats) {
  const NGrams& cgrams = c.ngrams();
  const NGrams& rgrams = r.ngrams();
  
  for(size_t i = 0; i < MAX_NGRAM_ORDER; i++) {
    size_t correct = 0;
    
    // if there were common n-1-grams there can be common n-grams
    if(i == 0 || (i > 0 && stats[(i - 1) * 3] > 0)) 
      countCommon(cgrams[i], rgrams[i], correct);
    
    stats[i * 3]     += correct;
    stats[i * 3 + 1] += cgrams[i].size();
    stats[i * 3 + 2] += rgrams[i].size();
  }
  
  //std::cout << stats << std::endl;
}

float smoothing = 1.0;

float computeBLEU2(const Stats& stats) {
  UTIL_THROW_IF(stats.size() != MAX_NGRAM_ORDER * 3, util::Exception, "Error");

  float logbleu1 = 0.0;
  float logbleu2 = 0.0;
  for (size_t i = 0; i < MAX_NGRAM_ORDER; ++i) {
    logbleu1 += log(stats[3*i] + smoothing) - log(stats[3*i+1] + smoothing);
    logbleu2 += log(stats[3*i] + smoothing) - log(stats[3*i+2] + smoothing);
  }
  logbleu1 /= MAX_NGRAM_ORDER;
  logbleu2 /= MAX_NGRAM_ORDER;
  
  // reflength divided by test length
  const float brevity1 = 1.0 - static_cast<float>(stats[2]) / stats[1];
  if (brevity1 < 0.0) {
    logbleu1 += brevity1;
  }
  const float brevity2 = 1.0 - static_cast<float>(stats[1]) / stats[2];
  if (brevity2 < 0.0) {
    logbleu2 += brevity2;
  }

  return exp((logbleu1 + logbleu2)/2);
}

float computeBLEU2(const Sentence& c, const Sentence& r) {
  if(c.size() == 0 || r.size() == 0)
    return 0;
  
  Stats stats;
  computeBLEU2stats(c, r, stats);
  return computeBLEU2(stats);
}

std::vector< std::vector<float> > seen;
std::vector< std::vector< std::pair<size_t, size_t> > > prev;

float S(size_t i, size_t j, Corpus& s, Corpus& t, bool slowRun = true) {  
  if(i == 0 || j == 0)
    return 0;
  
  if(seen[i][j] != -100)
    return seen[i][j];
  
  // Used to create first pass 1-1 alignment
  size_t fast[3][2] = { {0,1}, {1,0}, {1,1} }; 
  
  // Used to create full alignment
  size_t slow[10][2] = { {0,1}, {1,0}, {1,1},
                       {1,2}, {2,1}, {2,2},
                       {1,3}, {3,1}, {1,4},
                       {4,1} }; 
  
  size_t (*rungTypes)[2] = fast;
  if(slowRun)
    rungTypes = slow;
  
  float bestBLEU = 0;
  size_t bestIType = rungTypes[0][0];
  size_t bestJType = rungTypes[0][1];
  
  for(int k = 0; k < (slowRun ? 10 : 3); k++) {
    size_t iType = rungTypes[k][0];
    size_t jType = rungTypes[k][1];
    
    float result = -10;
    if(i >= iType && j >= jType) {
      result = S(i-iType, j-jType, s, t, slowRun)
               + computeBLEU2(s(i-iType, i-1), t(j-jType, j-1));
    
      if(result > bestBLEU) {
        bestBLEU = result;
        bestIType = iType;
        bestJType = jType;
      }
    }
  }
  
  seen[i][j] = bestBLEU;
  prev[i][j] = std::make_pair(bestIType, bestJType);
  return bestBLEU;
}

struct Rung {
  size_t i;
  size_t j;

  size_t iType;
  size_t jType;
};

void backTrack(size_t i, size_t j, std::vector<Rung>& rungs) {
  std::pair<size_t, size_t> p = prev[i][j];
  if(i > p.first && j > p.second) 
    backTrack(i - p.first, j - p.second, rungs);
  
  Rung rung;
  rung.i = i;
  rung.j = j;
  rung.iType = p.first;
  rung.jType = p.second;
  rungs.push_back(rung);
}

int main(int argc, char** argv)
{
  bool help;
  
  std::string sourceFileName;
  std::string targetFileName;

  std::string sourceFileNameOrig;
  std::string targetFileNameOrig;

  bool ladder;
  
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help,h", po::value(&help)->zero_tokens()->default_value(false), "Print this help message and exit")
    ("source,s", po::value<std::string>(&sourceFileName)->required(), "source language file (Processed)")
    ("target,t", po::value<std::string>(&targetFileName)->required(), "target language file (Processed)")
    ("Source,S", po::value<std::string>(&sourceFileNameOrig), "source language file (Original), if given will replace output of --source")
    ("Target,T", po::value<std::string>(&targetFileNameOrig), "target language file (Original), if given will replace output of --target")
    ("ladder,l", po::value(&ladder)->zero_tokens()->default_value(false), "Output in hunalign ladder format")
  ;

  po::options_description cmdline_options;
  cmdline_options.add(desc);
  po::variables_map vm;
  
  try { 
    po::store(po::command_line_parser(argc,argv).
              options(cmdline_options).run(), vm);
    po::notify(vm);
  }
  catch (std::exception& e) {
    std::cout << "Error: " << e.what() << std::endl << std::endl;
    
    std::cout << "Usage: " + std::string(argv[0]) +  " [options]" << std::endl;
    std::cout << desc << std::endl;
    exit(0);
  }
  
  if (help) {
    std::cout << "Usage: " + std::string(argv[0]) +  " [options]" << std::endl;
    std::cout << desc << std::endl;
    exit(0);
  }
  
  Corpus* sourceProc = new Corpus(sourceFileName);
  Corpus* targetProc = new Corpus(targetFileName);
  
  Corpus* sourceOrig = sourceProc;
  if(sourceFileNameOrig.size())
    sourceOrig = new Corpus(sourceFileNameOrig);
  
  Corpus* targetOrig = targetProc;
  if(targetFileNameOrig.size())
    targetOrig = new Corpus(targetFileNameOrig);
  
  seen.resize(sourceProc->size() + 1, std::vector<float>(targetProc->size() + 1, -100) );
  prev.resize(sourceProc->size() + 1, std::vector< std::pair<size_t,size_t> >(targetProc->size() + 1, std::make_pair(-1,-1)) );
  
  S(sourceProc->size(), targetProc->size(), *sourceProc, *targetProc);
    
  std::vector<Rung> rungs;
  backTrack(sourceProc->size(), targetProc->size(), rungs);
  
  float bleuSum = 0;
  size_t keptRungs = 0;
    
  size_t iLadder = 0;
  size_t jLadder = 0;
  
  for(size_t i = 0; i < rungs.size(); i++) {
    Rung r = rungs[i];
    if(r.iType && r.jType) {
      Sentence s1Proc = (*sourceProc)[r.i - r.iType] + (*sourceProc)[r.i - 1];
      Sentence s2Proc = (*targetProc)[r.j - r.jType] + (*targetProc)[r.j - 1];
      float bleu = computeBLEU2(s1Proc, s2Proc);
      
      if(ladder) {
        std::cout << iLadder << "\t" << jLadder << "\t" << bleu << std::endl;
      }
      else {
        Sentence s1Orig = (*sourceOrig)[r.i - r.iType] + (*sourceOrig)[r.i - 1];
        Sentence s2Orig = (*targetOrig)[r.j - r.jType] + (*targetOrig)[r.j - 1];
        std::cout << r.iType << "-" << r.jType << "\t" << bleu <<  "\t" << s1Orig << "\t" << s2Orig << std::endl;
      }
            
      bleuSum += bleu;
      keptRungs++;
    }
    if(ladder) {
      if(r.iType && !r.jType) {
        std::cout << iLadder << "\t" << jLadder << "\t" << -1 << std::endl;
        //Sentence s1 = s[i - p.first + 1] + s[i];
        //std::cout << p.first << "-" << 0 << "\t" << 0 <<  "\t" << s1 << "\t" << std::endl;
      }
      if(!r.iType && r.jType) {
        std::cout << iLadder << "\t" << jLadder << "\t" << -1 << std::endl;
        //Sentence s2 = t[j - p.second + 1] + t[j];
        //std::cout << 0 << "-" << 1 << "\t" << 0 <<  "\t" << "\t" << s2 << std::endl;
      }
    }
    
    iLadder += r.iType;
    jLadder += r.jType;

  }
  std::cerr << "Quality " << bleuSum/keptRungs << "/" << bleuSum/rungs.size() << std::endl;
}
