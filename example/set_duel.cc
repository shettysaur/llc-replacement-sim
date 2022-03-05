////////////////////////////////////////////
//                                        //
//     SRRIP [Jaleel et al. ISCA' 10]     //
//     Jinchun Kim, cienlux@tamu.edu      //
//                                        //
////////////////////////////////////////////
//
#include "../inc/champsim_crc2.h"

#define SHIP_LEADER 0
#define RED_LEADER 1
#define FOLLOWER 2
#define NUM_OFFSET_BITS 5
#define NUM_SAMPLED_LEADER_SETS (1 << NUM_OFFSET_BITS)
#define PSEL_BITS 14

uint32_t psel;
uint32_t ship_leader_count;
uint32_t red_leader_count;

//Ship++ Declarations
#define NUM_CORE 4
#define LLC_SET_BITS 11
#define MAX_LLC_SETS 8192
#define LLC_WAYS 16

#define SAT_INC(x,max)  (x<max)?x+1:x
#define SAT_DEC(x)      (x>0)?x-1:x
#define TRUE 1
#define FALSE 0

#define RRIP_OVERRIDE_PERC   0

// The base policy is SRRIP. SHIP needs the following on a per-line basis
#define maxRRPV 3
uint32_t rrpv[MAX_LLC_SETS][LLC_WAYS];
uint32_t is_prefetch[MAX_LLC_SETS][LLC_WAYS];
uint32_t fill_core[MAX_LLC_SETS][LLC_WAYS];

// These two are only for sampled sets (we use 64 sets)
#define NUM_LEADER_SETS   64

uint32_t ship_sample[MAX_LLC_SETS];
uint32_t line_reuse[MAX_LLC_SETS][LLC_WAYS];
uint64_t line_sig[MAX_LLC_SETS][LLC_WAYS];
	
// SHCT. Signature History Counter Table
// per-core 16K entry. 14-bit signature = 16k entry. 3-bit per entry
#define maxSHCTR 7
#define SHCT_SIZE (1<<14)
uint32_t SHCT[NUM_CORE][SHCT_SIZE];

// Statistics
uint64_t insertion_distrib[NUM_TYPES][maxRRPV+1];
uint64_t total_prefetch_downgrades;

//ReD Declarations
#define RED_SETS_BITS 8
#define RED_WAYS 16
#define RED_TAG_SIZE_BITS 11
#define RED_SECTOR_SIZE_BITS 2
#define RED_PC_FRACTION 4
#define PCs_BITS 8

#define RED_SETS (1<<RED_SETS_BITS)
#define RED_TAG_SIZE (1<<RED_TAG_SIZE_BITS)
#define RED_SECTOR_SIZE (1<<RED_SECTOR_SIZE_BITS)
#define PCs (1<<PCs_BITS)

// ReD 

struct ReD_ART_bl {
	uint64_t tag;                       // RED_TAG_SIZE_BITS
	uint8_t valid[RED_SECTOR_SIZE];     // 1 bit
};

struct ReD_ART_set {
	struct ReD_ART_bl adds[RED_WAYS];
	uint32_t insert;                    // 4 bits, log2(RED_WAYS)
};

struct ReD_ART_set ReD_ART[NUM_CORE][RED_SETS];

struct ReD_ART_PCbl {
	uint64_t pc_entry[RED_SECTOR_SIZE];  // PCs_BITS
};

struct ReD_ART_PCbl ReD_ART_PC[NUM_CORE][RED_SETS/RED_PC_FRACTION][RED_WAYS];

struct ReD_PCRT_bl {
	uint32_t reuse_counter,            // 10-bit counter
			 noreuse_counter;          // 10-bit counter
};

struct ReD_PCRT_bl ReD_PCRT[NUM_CORE][PCs];

uint32_t misses[NUM_CORE];

uint8_t set_type(uint32_t set){

	// uint32_t offsetMask = ((1 << (LLC_SET_BITS - NUM_OFFSET_BITS)) - 1);
	if(set % 32 == 0 || set % 32 == 30){
		return SHIP_LEADER;
	}
	else if(set % 32 == 31 || set % 32 == 1){
		return RED_LEADER;
	}
	return FOLLOWER;
}

// search for an address in ReD_ART
uint8_t lookup(uint32_t cpu, uint64_t PCnow, uint64_t block)
{
	uint64_t PCin_entry;
	uint64_t i, tag, subsector;
	uint64_t ART_set;
	
	subsector=block & (RED_SECTOR_SIZE-1);
	ART_set = (block>>RED_SECTOR_SIZE_BITS) & (RED_SETS -1);
	tag=(block>>(RED_SETS_BITS+RED_SECTOR_SIZE_BITS)) & (RED_TAG_SIZE-1);

	misses[cpu]++;
	
	for (i=0; i<RED_WAYS; i++) {
		if ((ReD_ART[cpu][ART_set].adds[i].tag == tag) && (ReD_ART[cpu][ART_set].adds[i].valid[subsector] == 1)) {
			if (ART_set % RED_PC_FRACTION == 0) {
				// if ART set stores PCs, count the reuse in PCRT
				PCin_entry = ReD_ART_PC[cpu][ART_set/RED_PC_FRACTION][i].pc_entry[subsector];
				ReD_PCRT[cpu][PCin_entry].reuse_counter++;

				if (ReD_PCRT[cpu][PCin_entry].reuse_counter > 1023) {
					// 10-bit counters, shift when saturated
					ReD_PCRT[cpu][PCin_entry].reuse_counter>>=1;
					ReD_PCRT[cpu][PCin_entry].noreuse_counter>>=1;
				}
				// Mark as invalid to count only once
				ReD_ART[cpu][ART_set].adds[i].valid[subsector] = 0; 
			}
			// found
			return 1;
		}
	}

	// not found
	return 0;
}

// remember a block in ReD_ART
void remember(uint32_t cpu, uint64_t PCnow, uint64_t block)
{
	uint32_t where;
	uint64_t i, tag, subsector, PCev_entry, PCnow_entry;
	uint64_t ART_set;
	
	subsector=block & (RED_SECTOR_SIZE-1);
	ART_set = (block>>RED_SECTOR_SIZE_BITS) & (RED_SETS -1);
	tag=(block>>(RED_SETS_BITS+RED_SECTOR_SIZE_BITS)) & (RED_TAG_SIZE-1);

	PCnow_entry = (PCnow >> 2) & (PCs-1);

	// Look first for the tag in my set
	for (i=0; i<RED_WAYS; i++) {
		if (ReD_ART[cpu][ART_set].adds[i].tag == tag)
			break;
	}
	
	if (i != RED_WAYS) {
		// Tag found, remember in the specific subsector
		ReD_ART[cpu][ART_set].adds[i].valid[subsector] = 1;

		if (ART_set % RED_PC_FRACTION == 0) {
			ReD_ART_PC[cpu][ART_set/RED_PC_FRACTION][i].pc_entry[subsector] = PCnow_entry;
		}
	}
	else {
		// Tag not found, need to replace entry in ART
		where = ReD_ART[cpu][ART_set].insert;
		
		if (ART_set % RED_PC_FRACTION == 0) {
			// if ART set stores PCs, count noreuse of evicted PCs if needed
			for (int s=0; s<RED_SECTOR_SIZE; s++) {
				if (ReD_ART[cpu][ART_set].adds[where].valid[s]) {
					PCev_entry = ReD_ART_PC[cpu][ART_set/RED_PC_FRACTION][where].pc_entry[s];
					ReD_PCRT[cpu][PCev_entry].noreuse_counter++;

					// 10-bit counters, shift when saturated
					if (ReD_PCRT[cpu][PCev_entry].noreuse_counter > 1023) {
						ReD_PCRT[cpu][PCev_entry].reuse_counter>>=1;
						ReD_PCRT[cpu][PCev_entry].noreuse_counter>>=1;
					}
				}
			}
		}
		
		// replace entry to store new block address
		
		ReD_ART[cpu][ART_set].adds[where].tag = tag;
		for (int j=0; j<RED_SECTOR_SIZE; j++) {
			ReD_ART[cpu][ART_set].adds[where].valid[j] = 0;
		}
		ReD_ART[cpu][ART_set].adds[where].valid[subsector] = 1;
		
		if (ART_set % RED_PC_FRACTION == 0) {
			ReD_ART_PC[cpu][ART_set/RED_PC_FRACTION][where].pc_entry[subsector] = PCnow_entry;
		}
		
		// update pointer to next entry to replace
		ReD_ART[cpu][ART_set].insert++;
		if (ReD_ART[cpu][ART_set].insert == RED_WAYS) 
			ReD_ART[cpu][ART_set].insert = 0;
	}
}


// initialize replacement state
void InitReplacementState()
{
	psel = 1 << (PSEL_BITS-1);
	ship_leader_count = 0;
	red_leader_count = 0;
	cout << "PSEL init : " << psel << endl;

	//Ship++ init
    int LLC_SETS = (get_config_number() <= 2) ? 2048 : MAX_LLC_SETS;

    for (int i=0; i<NUM_CORE; i++) {
        for (int j=0; j<SHCT_SIZE; j++) {
            SHCT[i][j] = 1; // Assume weakly re-use start
        }
    }

    int leaders=0;

    while(leaders<NUM_LEADER_SETS){
      int randval = rand()%LLC_SETS;
      
      if(ship_sample[randval]==0){
	ship_sample[randval]=1;
	leaders++;
      }
    }

	// ReD init
	for (int core=0; core<NUM_CORE; core++) {
		for (int i=0; i<RED_SETS; i++) {
			ReD_ART[core][i].insert = 0;
			for (int j=0; j<RED_WAYS; j++) {
				ReD_ART[core][i].adds[j].tag=0;
				for (int k=0; k<RED_SECTOR_SIZE; k++) {
					ReD_ART[core][i].adds[j].valid[k] = 0;
				}
			}
		}
	}

	for (int core=0; core<NUM_CORE; core++) {
		for (int i=0; i<RED_SETS/RED_PC_FRACTION; i++) {
			for (int j=0; j<RED_WAYS; j++) {
				for (int k=0; k<RED_SECTOR_SIZE; k++) {
					ReD_ART_PC[core][i][j].pc_entry[k] = 0;
				}
			}
		}
	}

	for (int core=0; core<NUM_CORE; core++) {
		for (int i=0; i<PCs; i++) {
			ReD_PCRT[core][i].reuse_counter = 3;
			ReD_PCRT[core][i].noreuse_counter = 0;
		}
	}

	for (int i=0; i<NUM_CORE; i++) {
	   misses[i]=0; 
	}
	
	// SRRIP init
	cout << "Initialize SRRIP state" << endl;
	for (int i=0; i<MAX_LLC_SETS; i++) {
		for (int j=0; j<LLC_WAYS; j++) {
			rrpv[i][j] = maxRRPV;
			line_reuse[i][j] = FALSE;
            is_prefetch[i][j] = FALSE;
            line_sig[i][j] = 0;
		}
	}
}

uint32_t GetVictimInSetRED (uint32_t cpu, uint32_t set, uint64_t PC, uint64_t paddr, uint32_t type){
	uint8_t present;
	uint64_t PCentry;
	uint64_t block;

	block = paddr >> 6; // assuming 64B line size, get rid of lower bits
	PCentry = (PC >> 2) & (PCs-1);
    	
	if (type == LOAD || type == RFO || type == PREFETCH) {
		present = lookup(cpu, PC, block);
		if (!present) {
			// Remember in ART only if reuse in PCRT is intermediate, or one out of eight times
			if (   (    ReD_PCRT[cpu][PCentry].reuse_counter * 64 > ReD_PCRT[cpu][PCentry].noreuse_counter
					 && ReD_PCRT[cpu][PCentry].reuse_counter * 3 < ReD_PCRT[cpu][PCentry].noreuse_counter)
				|| (misses[cpu] % 8 == 0)) 
			{
				remember(cpu, PC, block);
			}
			// bypass when address not in ART and reuse in PCRT is low or intermediate
			if (ReD_PCRT[cpu][PCentry].reuse_counter * 3 < ReD_PCRT[cpu][PCentry].noreuse_counter) {
				/* BYPASS */
				return LLC_WAYS;                         
			}
		} 
	}
	
	// NO BYPASS when address is present in ART or when reuse is high in PCRT
	// or for write-backs
	
	// SRRIP, look for the maxRRPV line
	while (1) {
		for (int i=0; i<LLC_WAYS; i++)
			if (rrpv[set][i] == maxRRPV)
				return i;

		for (int i=0; i<LLC_WAYS; i++)
			rrpv[set][i]++;
	}

	return 0;
}

uint32_t GetVictimInSetShip (uint32_t cpu, uint32_t set, uint64_t PC, uint64_t paddr, uint32_t type){
	// look for the maxRRPV line
    while (1)
    {
        for (int i=0; i<LLC_WAYS; i++)
            if (rrpv[set][i] == maxRRPV) { // found victim
                return i;
            }

        for (int i=0; i<LLC_WAYS; i++)
            rrpv[set][i]++;
    }

    // WE SHOULD NOT REACH HERE
    assert(0);
    return 0;
}


// find replacement victim
// return value should be 0 ~ 15 or 16 (bypass)
uint32_t GetVictimInSet (uint32_t cpu, uint32_t set, const BLOCK *current_set, uint64_t PC, uint64_t paddr, uint32_t type)
{
	if((set_type(set) == SHIP_LEADER) || ((set_type(set) == FOLLOWER) && (psel < 1 << (PSEL_BITS-1)))){
	// if(1){
		ship_leader_count++;
	    return GetVictimInSetShip(cpu, set, PC, paddr, type);
	}
	else{
		red_leader_count++;
		return GetVictimInSetRED(cpu, set, PC, paddr, type);
	}
}

void UpdateReplacementStateShip (uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr, uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit){
	uint32_t sig   = line_sig[set][way];
    if (hit) { // update to REREF on hit
        if( type != WRITEBACK ) 
        {

            if( (type == PREFETCH) && is_prefetch[set][way] )
            {
                if( (ship_sample[set] == 1) && ((rand()%100 <5) || (get_config_number()==4))) 
                {
                    uint32_t fill_cpu = fill_core[set][way];

                    SHCT[fill_cpu][sig] = SAT_INC(SHCT[fill_cpu][sig], maxSHCTR);
                    line_reuse[set][way] = TRUE;
                }
            }
            else 
            {
                rrpv[set][way] = 0;

                if( is_prefetch[set][way] )
                {
                    rrpv[set][way] = maxRRPV;
                    is_prefetch[set][way] = FALSE;
                    total_prefetch_downgrades++;
                }

                if( (ship_sample[set] == 1) && (line_reuse[set][way]==0) ) 
                {
                    uint32_t fill_cpu = fill_core[set][way];

                    SHCT[fill_cpu][sig] = SAT_INC(SHCT[fill_cpu][sig], maxSHCTR);
                    line_reuse[set][way] = TRUE;
                }
            }
        }
        
	return;
    }
    
    //--- All of the below is done only on misses -------
    // remember signature of what is being inserted
    uint64_t use_PC = (type == PREFETCH ) ? ((PC << 1) + 1) : (PC<<1);
    uint32_t new_sig = use_PC%SHCT_SIZE;
    
    if( ship_sample[set] == 1 ) 
    {
        uint32_t fill_cpu = fill_core[set][way];
        
        // update signature based on what is getting evicted
        if (line_reuse[set][way] == FALSE) { 
            SHCT[fill_cpu][sig] = SAT_DEC(SHCT[fill_cpu][sig]);
        }
        else 
        {
            SHCT[fill_cpu][sig] = SAT_INC(SHCT[fill_cpu][sig], maxSHCTR);
        }

        line_reuse[set][way] = FALSE;
        line_sig[set][way]   = new_sig;  
        fill_core[set][way]  = cpu;
    }



    is_prefetch[set][way] = (type == PREFETCH);

    // Now determine the insertion prediciton

    uint32_t priority_RRPV = maxRRPV-1 ; // default SHIP

    if( type == WRITEBACK )
    {
        rrpv[set][way] = maxRRPV;
    }
    else if (SHCT[cpu][new_sig] == 0) {
      rrpv[set][way] = (rand()%100>=RRIP_OVERRIDE_PERC)?  maxRRPV: priority_RRPV; //LowPriorityInstallMostly
    }
    else if (SHCT[cpu][new_sig] == 7) {
        rrpv[set][way] = (type == PREFETCH) ? 1 : 0; // HighPriority Install
    }
    else {
        rrpv[set][way] = priority_RRPV; // HighPriority Install 
    }

    // Stat tracking for what insertion it was at
    insertion_distrib[type][rrpv[set][way]]++;
}

void UpdateReplacementStateRED (uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr, uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit){
	// Do not update when bypassing
	if (way == 16) return;  

	// Write-backs do not change rrpv
	if (type == WRITEBACK) return; 

	// SRRIP
	if (hit)
		rrpv[set][way] = 0;
	else
		rrpv[set][way] = maxRRPV-1;
}

// called on every cache hit and cache fill
void UpdateReplacementState (uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr, uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
	if(set_type(set) == SHIP_LEADER && !hit){
		psel = SAT_INC(psel,(1 << PSEL_BITS));
	}
	else if(set_type(set) == RED_LEADER && !hit){
		psel = SAT_DEC(psel);
	}
	if((set_type(set) == SHIP_LEADER) || ((set_type(set) == FOLLOWER) && (psel < 512))){
	    UpdateReplacementStateShip(cpu, set, way, paddr, PC, victim_addr, type, hit);
	}
	else{
		UpdateReplacementStateRED(cpu, set, way, paddr, PC, victim_addr, type, hit);
	}
	// UpdateReplacementStateShip(cpu, set, way, paddr, PC, victim_addr, type, hit);
	// UpdateReplacementStateRED(cpu, set, way, paddr, PC, victim_addr, type, hit);
}

// use this function to print out your own stats on every heartbeat 
void PrintStats_Heartbeat()
{
	cout << "PSEL : " << psel << endl; 
	cout << "Ship Leader Count : " << ship_leader_count << endl; 
	cout << "Read Leader Count : " << red_leader_count << endl; 
}

// use this function to print out your own stats at the end of simulation
void PrintStats()
{

}