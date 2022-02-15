////////////////////////////////////////////////////////////
//                                                        //
//     RED (REuse Detector) replacement policy            //
//     Javier Diaz, jdmaag@gmail.com                      //
//     Pablo Iba�ez, imarin@unizar.es                     //
//                                                        //
////////////////////////////////////////////////////////////

#include "../inc/champsim_crc2.h"

// NUM_CORE and LLC_SETS are the maximum, used to declare variables and initialize data structures
// During simulation, only the actual cpu and llc set is used, so it is self-limited in configs 1 and 2
#define NUM_CORE 4
#define LLC_SETS NUM_CORE*2048
#define LLC_WAYS 16

#define RED_SETS_BITS 9
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

// SRRIP

#define maxRRPV 3
uint32_t rrpv[LLC_SETS][LLC_WAYS];      // 2 bits

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
	for (int i=0; i<LLC_SETS; i++) {
		for (int j=0; j<LLC_WAYS; j++) {
			rrpv[i][j] = maxRRPV;
		}
	}
}


// find replacement victim
// return value should be 0 ~ 15 or 16 (bypass)
uint32_t GetVictimInSet (uint32_t cpu, uint32_t set, const BLOCK *current_set, uint64_t PC, uint64_t paddr, uint32_t type)
{
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

// called on every cache hit and cache fill
void UpdateReplacementState (uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr, uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
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

// use this function to print out your own stats on every heartbeat 
void PrintStats_Heartbeat()
{

}

// use this function to print out your own stats at the end of simulation
void PrintStats()
{

}
