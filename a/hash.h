struct hashmap {
  int size;
  struct linkedlist** buckets;
  pthread_mutex_t lrock;
};

unsigned long hash(unsigned char *);
void makeHashMap(struct hashmap* );
int checkIfPresent(char*, struct hashmap*);
int put(char*, struct hashmap*);