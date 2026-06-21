1. Start Program
2. START DISCOVERY PHASE
2.1 Discovery loads hardware from before (All backends scan here and NOWHERE else)
2.2 load mapping defs 
2.3 discover hardware and hash the unique set of values to UUID, compare to already existing UUID  this is the ONLY part where catalog is modified nowhere else, if it doesnt exist add, if it does exist do nothing
2.4  validate that all UUID referenced in mappings exist in catalog
3. MAPPING PHASE
4. RT PHASE