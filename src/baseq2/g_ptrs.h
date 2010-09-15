typedef enum {
    P_bad,

    P_prethink,
    P_think,
    P_blocked,
    P_touch,
    P_use,
    P_pain,
    P_die,

    P_moveinfo_endfunc,

    P_monsterinfo_currentmove,
    P_monsterinfo_stand,
    P_monsterinfo_idle,
    P_monsterinfo_search,
    P_monsterinfo_walk,
    P_monsterinfo_run,
    P_monsterinfo_dodge,
    P_monsterinfo_attack,
    P_monsterinfo_melee,
    P_monsterinfo_sight,
    P_monsterinfo_checkattack
} ptr_type_t;

typedef struct {
    ptr_type_t type;
    void *ptr;
} save_ptr_t;

extern const save_ptr_t save_ptrs[];
extern const int num_save_ptrs;
