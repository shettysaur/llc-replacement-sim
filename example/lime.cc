////////////////////////////////////////////
//                                        //
//        LIME replacement policy         //
//                                        //
////////////////////////////////////////////

#include "../inc/champsim_crc2.h"
//#include "BloomFilter.h"
#include <set>
#include <vector>
#include <array>
#include <cstdint>
#include <string>

using namespace std;

/************MurmurHash3_x64_128********************/
#define ROTL64(x,y) rotl64(x,y)

inline uint64_t rotl64 ( uint64_t x, int8_t r )
{
  return (x << r) | (x >> (64 - r));
}   

inline uint64_t getblock ( const uint64_t * p, int i )
{
  return p[i];
}

inline uint64_t fmix64 ( uint64_t k )
{
  k ^= k >> 33;
  k *= 0xff51afd7ed558ccd;
  k ^= k >> 33;
  k *= 0xc4ceb9fe1a85ec53;
  k ^= k >> 33;

  return k;
}

void MurmurHash3_x64_128 ( const void * key, const int len,
                           const uint32_t seed, void * out )
{
  const uint8_t * data = (const uint8_t*)key;
  const int nblocks = len / 16;                                                  
  int i;

  uint64_t h1 = seed;
  uint64_t h2 = seed;

  uint64_t c1 = 0x87c37b91114253d5; //big constant
  uint64_t c2 = 0x4cf5ad432745937f; //big constant

  const uint64_t * blocks = (const uint64_t *)(data);

  for(i = 0; i < nblocks; i++)
  {
    uint64_t k1 = getblock(blocks,i*2+0);
    uint64_t k2 = getblock(blocks,i*2+1);
    
    k1 *= c1; k1  = ROTL64(k1,31); k1 *= c2; h1 ^= k1;
    
    h1 = ROTL64(h1,27); h1 += h2; h1 = h1*5+0x52dce729;
    
    k2 *= c2; k2  = ROTL64(k2,33); k2 *= c1; h2 ^= k2;
    
    h2 = ROTL64(h2,31); h2 += h1; h2 = h2*5+0x38495ab5;
  }

  const uint8_t * tail = (const uint8_t*)(data + nblocks*16);

  uint64_t k1 = 0;
  uint64_t k2 = 0;

  switch(len & 15)
  {
  case 15: k2 ^= (uint64_t)(tail[14]) << 48;
  case 14: k2 ^= (uint64_t)(tail[13]) << 40;
  case 13: k2 ^= (uint64_t)(tail[12]) << 32;
  case 12: k2 ^= (uint64_t)(tail[11]) << 24;
  case 11: k2 ^= (uint64_t)(tail[10]) << 16;
  case 10: k2 ^= (uint64_t)(tail[ 9]) << 8;
  case  9: k2 ^= (uint64_t)(tail[ 8]) << 0;
           k2 *= c2; k2  = ROTL64(k2,33); k2 *= c1; h2 ^= k2;

  case  8: k1 ^= (uint64_t)(tail[ 7]) << 56;
  case  7: k1 ^= (uint64_t)(tail[ 6]) << 48;
  case  6: k1 ^= (uint64_t)(tail[ 5]) << 40;
  case  5: k1 ^= (uint64_t)(tail[ 4]) << 32;
  case  4: k1 ^= (uint64_t)(tail[ 3]) << 24;
  case  3: k1 ^= (uint64_t)(tail[ 2]) << 16;
  case  2: k1 ^= (uint64_t)(tail[ 1]) << 8;
  case  1: k1 ^= (uint64_t)(tail[ 0]) << 0;
           k1 *= c1; k1  = ROTL64(k1,31); k1 *= c2; h1 ^= k1;
  };

  h1 ^= len; h2 ^= len;

  h1 += h2;
  h2 += h1;

  h1 = fmix64(h1);
  h2 = fmix64(h2);

  h1 += h2;
  h2 += h1;

  ((uint64_t*)out)[0] = h1;
  ((uint64_t*)out)[1] = h2;
}


/*********BloomFilter********************/
class BloomFilter {
public:
  BloomFilter();
  BloomFilter(uint64_t size, uint8_t numHashes);
  void add(uint64_t data);
//  static BloomFilter merge(BloomFilter b1, BloomFilter b2);
  bool possiblyContains(uint64_t data) const;
  void clear();
  uint8_t getCount();

  uint64_t getSize();
//  uint8_t getNumHashes();
//  void setSize(uint64_t s);
//  void setNumHashes(uint8_t n);
//  uint64_t getValue();
//  void setValue(uint64_t value);
//  std::string toString();

private:
  uint8_t m_numHashes;
  uint8_t count;
  std::vector<bool> m_bits;
};

BloomFilter::BloomFilter()
      : m_numHashes(6)
      , m_bits(std::vector<bool>(1107, false))
{
 
}

BloomFilter::BloomFilter(uint64_t size, uint8_t numHashes)
      : m_numHashes(numHashes)
      , m_bits(std::vector<bool>(size, false))
{
    count = 0;
}

std::array<uint64_t,2> hashbf(const uint8_t *data, std::size_t len)
{
  std::array<uint64_t, 2> hashValue {0, 0};
  MurmurHash3_x64_128(data, len, 0, hashValue.data());
  return hashValue;
}

inline uint64_t nthHash(uint8_t n, uint64_t hashA, uint64_t hashB, uint64_t filterSize) 
{
    return (hashA + n * hashB) % filterSize;
}

void BloomFilter::add(uint64_t data) 
{
  std::array<uint8_t, 8> data_array {0, 0, 0, 0, 0, 0, 0, 0};
  for(int i = 0; i < 8; i++) {
    data_array[i] = ((data >> (i*8)) & 0x00FF);
  }
  auto hashValues = hashbf(data_array.data(), 8);

  for (int n = 0; n < m_numHashes; n++) 
  {
      m_bits[nthHash(n, hashValues[0], hashValues[1], m_bits.size())] = true;
  }
  count++;
}  

bool BloomFilter::possiblyContains(uint64_t data) const 
{
  std::array<uint8_t, 8> data_array {0, 0, 0, 0, 0, 0, 0, 0};
  for(int i = 0; i < 8; i++) {
    data_array[i] = ((data >> (i*8)) & 0x00FF);
  }
  auto hashValues = hashbf(data_array.data(), 8);

  for (int n = 0; n < m_numHashes; n++) 
  {
      if (!m_bits[nthHash(n, hashValues[0], hashValues[1], m_bits.size())]) 
      {
          return false;
      }
  }

  return true;
}

void BloomFilter::clear()
{
    count = 0;
    for (uint64_t i=0; i<m_bits.size();i++ )
        m_bits[i] = false;
}

uint8_t BloomFilter::getCount()
{
    return count;
}

uint64_t BloomFilter::getSize()
{
    return m_bits.size();
}

/******************LIME*******************/

int NUM_CORE; 
int LLC_SETS ;
int LLC_SETS_LOG2 ;
#define LLC_WAYS 16
#define maxRRPV 7

//Load/Store instruction category
#define RANDOM 0
#define STREAMING 1
#define THRASH 2
#define FRIENDLY 3

//
#define SAMPLE_COUNT 128 // LLC_WAYS<<3 
#define OV_size 128
int ALIAS_TAB_SIZE;

uint32_t rrpv[8192][LLC_WAYS];
uint64_t cold_misses;
vector<vector<uint32_t>> ov;
vector<vector<bool>> hv;
uint32_t id[8192];
vector<vector<pair<uint64_t, uint64_t>>> history;

BloomFilter** data_filter;
set<int> sample_set;
BloomFilter* pc_friendly_filter;
BloomFilter* pc_streaming_filter;
vector<pair<uint64_t,bool>> alias_table;
// initialize replacement state
void InitReplacementState()
{
    int cfg = get_config_number();
    if(cfg==1 || cfg==2){
        NUM_CORE=1;
        LLC_SETS  = NUM_CORE*2048;
        LLC_SETS_LOG2 = 11;
        ALIAS_TAB_SIZE = NUM_CORE*64 ;
    }
    else if(cfg == 3 || cfg == 4){
        NUM_CORE=4;
        LLC_SETS  = NUM_CORE*2048;
        LLC_SETS_LOG2 = 13;
        ALIAS_TAB_SIZE = NUM_CORE*64 ;
    
    }

    for (int i=0; i<LLC_SETS; i++) {
        for (int j=0; j<LLC_WAYS; j++) {
            rrpv[i][j] = maxRRPV;
        }
    }
    
    pc_friendly_filter = new BloomFilter(4096, 6);
    pc_streaming_filter = new BloomFilter(4096, 6);


    int sample_sets_num = 20*NUM_CORE;
    if(cfg==1 || cfg == 2){
        int random_number[20]={46, 102, 257, 406, 510, 630, 772, 880, 981, 1088, 1163, 1206, 1311, 1436, 1543, 1644, 1735, 1810, 1956, 2017};
        for(int i=0; i<sample_sets_num; i++){
            sample_set.insert(random_number[i]);
        }
    }
    else if(cfg==3 || cfg==4){
        int random_number[80]={187, 201, 286, 332, 449, 642, 675, 682, 744, 770, 883, 1013, 1085, 1227, 1237, 1298, 1310, 1333, 1358, 1671, 1728, 1812, 1819, 1963, 2295, 2410, 2450, 2508, 2553, 2592, 2700, 2976, 3084, 3359, 3557, 3780, 3879, 4303, 4328, 4397, 4428, 4514, 4561, 4852, 4897, 4943, 5000, 5187, 5314, 5361, 5532, 5535, 5635, 5646, 5718, 5853, 5898, 5917, 6056, 6164, 6340, 6435, 6827, 6839, 6852, 6944, 7174, 7466, 7494, 7496, 7497, 7519, 7524, 7604, 7638, 7788, 7830, 8152, 8158, 8173};
        for(int i=0; i<sample_sets_num; i++){
            sample_set.insert(random_number[i]);
        }
    }

    for (int i=0; i<LLC_SETS; i++) {
        std::vector<uint32_t> init;
        ov.push_back(init);
        std::vector<bool> init2;
        hv.push_back(init2);
    }
    
    for (int i=0; i<LLC_SETS; i++){
        std::vector<pair<uint64_t, uint64_t>> init;
        history.push_back(init);
    }

    for (int i=0; i<LLC_SETS; i++) {
        id[i] = 0;
    }

}

uint32_t hash18(uint64_t pc)
{
    uint16_t pc0 = pc & 0xffff;
    uint8_t bit32 = (pc & 0x100000000) >> 32;
    uint8_t bit40 = (pc & 0x10000000000) >> 40;
    uint16_t hash = pc0 + (bit40 <<18) + (bit32<<17) ;    
    return hash;
}

uint64_t hash37(uint64_t tag_addr)
{
    uint64_t top8 = (tag_addr<<(6+LLC_SETS_LOG2)) >> 56;
    uint64_t hash = (top8<<29) + (tag_addr&0x1fffffff);
    return hash;
}

int getPCCategory(uint64_t pc)
{
    uint32_t target = hash18(pc); //hash1 return 16 bit
    bool friendly = pc_friendly_filter -> possiblyContains(target); 
    bool streaming = pc_streaming_filter -> possiblyContains(target); 
    if(friendly && streaming){
        vector<pair<uint64_t,bool>>::iterator it;
        for(it=alias_table.begin(); it!=alias_table.end(); it++){
            if((*it).first == target){
                if((*it).second==true)
                    return FRIENDLY;
                else
                    return STREAMING;
            }
        }
        //put target into alias_table
        if(alias_table.size()>ALIAS_TAB_SIZE){
            alias_table.erase(alias_table.begin());
        }
        pair<uint64_t,bool> alias_pc(target,true);
        alias_table.push_back(alias_pc);
        return FRIENDLY;
    }
        //return THRASH;
    else if (friendly)
        return FRIENDLY;
    else if (streaming)
        return STREAMING;
    else 
        return RANDOM;
}

void updatePCCategory(uint32_t pc, int new_cate)
{
    if (new_cate == FRIENDLY)
        pc_friendly_filter -> add(pc);
    else if (new_cate == STREAMING)
        pc_streaming_filter -> add(pc);
   
    vector<pair<uint64_t,bool>>::iterator it; 
    for(it=alias_table.begin(); it!=alias_table.end(); it++){
        if((*it).first == pc){
            if (new_cate == FRIENDLY)
                (*it).second=true;
            if (new_cate == STREAMING)
                (*it).second=false;
        }
    }
    return;
}

// find replacement victim
// return value should be 0 ~ 15 or 16 (bypass)
uint32_t GetVictimInSet (uint32_t cpu, uint32_t set, const BLOCK *current_set, uint64_t PC, uint64_t paddr, uint32_t type)
{
    if(type == 3)
        return 0;

    if(type == 2)
        return LLC_WAYS;

    if(getPCCategory(PC) == STREAMING && type!=3){
        return LLC_WAYS;
    }

    // look for the maxRRPV line
    while (1)
    {
        for (int i=0; i<LLC_WAYS; i++)
            if (rrpv[set][i] == maxRRPV){
                return i;
            }

        for (int i=0; i<LLC_WAYS; i++)
            rrpv[set][i]++;
    }

    // WE SHOULD NOT REACH HERE
    assert(0);
    return 0;

}

// called on every cache hit and cache fill
void UpdateReplacementState (uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr, uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
    if(type==3 || type==2) // do not train on writeback or prefetch
        return; 
    if(sample_set.find(set) != sample_set.end()){
/*
        if(set == 46)
            cout << "cfg 1/2" << endl;
        if(set == 7494)
            cout << "cfg 3/4" << endl;
*/
        uint64_t tag_addr = paddr >> (6 + LLC_SETS_LOG2);
            // ---- Update History
        if (history[set].size() >= OV_size) {
            if(hv[set][0] == false){
                int new_cate = 1; //STREAMING
                uint64_t ori_pc = history[set][0].second;
                updatePCCategory(ori_pc, new_cate);
            }
            history[set].erase( history[set].begin());
            ov[set].erase(ov[set].begin());
            hv[set].erase(hv[set].begin());
            id[set] --;
        } 


        // ---- Update OV HV
        pair<uint64_t, uint64_t> paddr_PC (hash37(tag_addr), hash18(PC));
        history[set].push_back(paddr_PC);
        ov[set].push_back(0);
        hv[set].push_back(false);

        int end = id[set];
        int start = -1;
        for (int i = end-1; i >=0; i--){
            if (history[set][i].first == paddr_PC.first){
                start = i;
                break;
            }
        }
        id[set] ++;
        if (start >= 0){
            bool keep = true;
            for(int i=start; i<end; i++){
                keep = keep && (ov[set][i]<LLC_WAYS-1);
                if(!keep)
                    break;
            }
            uint64_t ori_pc = history[set][start].second;
            int new_cate;
            if(keep){
                hv[set][start] = true;
                for (int i=start; i < end; i++)
                    ov[set][i] += 1;
                new_cate = FRIENDLY; //FRIENDLY
                updatePCCategory(ori_pc, new_cate);
            }else{
                new_cate = STREAMING; //STREAMING
                updatePCCategory(ori_pc, new_cate);
            }
        }
    }

    if(way == LLC_WAYS){
        return; 
    }
    int category = getPCCategory(PC);

    if(hit == 1){
        switch(category){
            case RANDOM:
                if(rrpv[set][way] >  (maxRRPV-4))
                    rrpv[set][way] = maxRRPV-4;
                break;
            case STREAMING:
                rrpv[set][way] = maxRRPV-4;
                break;
            case THRASH:
            case FRIENDLY:
                rrpv[set][way] = 0;
                break;
        }
    }
    else{
        switch(category){
            case RANDOM:
                rrpv[set][way] = maxRRPV-1;
                break;
            case STREAMING:
                rrpv[set][way] = maxRRPV-1;
                break;
            case THRASH:
            case FRIENDLY:
                rrpv[set][way] = 0;
                for(uint64_t i=0; i< LLC_WAYS; i++){
                    if(i != way && (rrpv[set][i] < 6))
                        rrpv[set][i]+=1;
                }
                break;
        }

    }
    return; 
}

// use this function to print out your own stats on every heartbeat 
void PrintStats_Heartbeat()
{

}

// use this function to print out your own stats at the end of simulation
void PrintStats()
{
}

