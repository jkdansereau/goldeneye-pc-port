
=== level_09 (level 9, UsetupsevbunkerZ) ===
Objective 0  [briefing text id 30749]  min difficulty: Agent
   - DESTROY_OBJECT       tag 0 -> CCTV
   - DESTROY_OBJECT       tag 1 -> CCTV
   - DESTROY_OBJECT       tag 2 -> CCTV
   - DESTROY_OBJECT       tag 3 -> CCTV
Objective 1  [briefing text id 30750]  min difficulty: Agent
   - COPY_ITEM            (key-analyzer/datavac copy flag)
Objective 2  [briefing text id 30751]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x100 (bit(s) [8])
   - FAIL_CONDITION       stage flag mask 0x200 (bit(s) [9]) -- mission FAILS when set
Objective 3  [briefing text id 30752]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x400 (bit(s) [10])
   - FAIL_CONDITION       stage flag mask 0x800 (bit(s) [11]) -- mission FAILS when set
   - FAIL_CONDITION       stage flag mask 0x200 (bit(s) [9]) -- mission FAILS when set
Objective 4  [briefing text id 30753]  min difficulty: Agent
   - PHOTOGRAPH           tag 5 -> MULTI_MONITOR; photo-flag word @8=0x0

=== level_20 (level 20, UsetupsiloZ) ===
Objective 0  [briefing text id 34834]  min difficulty: Agent
   - DEPOSIT_IN_ROOM      ref @4=34 ref @8=4; runtime word @c=0x0
   - DEPOSIT_IN_ROOM      ref @4=34 ref @8=12; runtime word @c=0x0
   - DEPOSIT_IN_ROOM      ref @4=34 ref @8=88; runtime word @c=0x0
   - DEPOSIT_IN_ROOM      ref @4=34 ref @8=95; runtime word @c=0x0
Objective 1  [briefing text id 34835]  min difficulty: Agent
   - PHOTOGRAPH           tag 0 -> PROP; photo-flag word @8=0x0
Objective 2  [briefing text id 34836]  min difficulty: Agent
   - COLLECT_OBJECT       tag 1 -> PROP
Objective 3  [briefing text id 34837]  min difficulty: Agent
   - COLLECT_OBJECT       tag 2 -> PROP
   - COLLECT_OBJECT       tag 3 -> PROP
   - COLLECT_OBJECT       tag 4 -> PROP
   - COLLECT_OBJECT       tag 5 -> PROP
   - FAIL_CONDITION       stage flag mask 0x100 (bit(s) [8]) -- mission FAILS when set
   - FAIL_CONDITION       stage flag mask 0x200 (bit(s) [9]) -- mission FAILS when set
   - FAIL_CONDITION       stage flag mask 0x400 (bit(s) [10]) -- mission FAILS when set
   - FAIL_CONDITION       stage flag mask 0x800 (bit(s) [11]) -- mission FAILS when set
Objective 4  [briefing text id 34838]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x8000 (bit(s) [15])
   - FAIL_CONDITION       stage flag mask 0x4000 (bit(s) [14]) -- mission FAILS when set

=== level_21 (level 21, UsetupsevbunkerZ) ===
Objective 0  [briefing text id 30749]  min difficulty: Agent
   - DESTROY_OBJECT       tag 0 -> CCTV
   - DESTROY_OBJECT       tag 1 -> CCTV
   - DESTROY_OBJECT       tag 2 -> CCTV
   - DESTROY_OBJECT       tag 3 -> CCTV
Objective 1  [briefing text id 30750]  min difficulty: Agent
   - COPY_ITEM            (key-analyzer/datavac copy flag)
Objective 2  [briefing text id 30751]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x100 (bit(s) [8])
   - FAIL_CONDITION       stage flag mask 0x200 (bit(s) [9]) -- mission FAILS when set
Objective 3  [briefing text id 30752]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x400 (bit(s) [10])
   - FAIL_CONDITION       stage flag mask 0x800 (bit(s) [11]) -- mission FAILS when set
   - FAIL_CONDITION       stage flag mask 0x200 (bit(s) [9]) -- mission FAILS when set
Objective 4  [briefing text id 30753]  min difficulty: Agent
   - PHOTOGRAPH           tag 5 -> MULTI_MONITOR; photo-flag word @8=0x0

=== level_22 (level 22, UsetupstatueZ) ===
Objective 0  [briefing text id 35897]  min difficulty: Agent
   - FAIL_CONDITION       stage flag mask 0x200000 (bit(s) [21]) -- mission FAILS when set
   - COMPLETE_CONDITION   stage flag mask 0x100 (bit(s) [8])
Objective 1  [briefing text id 35898]  min difficulty: Agent
   - FAIL_CONDITION       stage flag mask 0x20000 (bit(s) [17]) -- mission FAILS when set
   - FAIL_CONDITION       stage flag mask 0x200000 (bit(s) [21]) -- mission FAILS when set
   - COMPLETE_CONDITION   stage flag mask 0x400 (bit(s) [10])
Objective 2  [briefing text id 35899]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x200 (bit(s) [9])
   - FAIL_CONDITION       stage flag mask 0x200000 (bit(s) [21]) -- mission FAILS when set
Objective 3  [briefing text id 35900]  min difficulty: Agent
   - FAIL_CONDITION       stage flag mask 0x400000 (bit(s) [22]) -- mission FAILS when set
   - FAIL_CONDITION       stage flag mask 0x200000 (bit(s) [21]) -- mission FAILS when set
   - COMPLETE_CONDITION   stage flag mask 0x800 (bit(s) [11])
Objective 4  [briefing text id 35901]  min difficulty: Agent
   - COLLECT_OBJECT       tag 1 -> PROP
   - FAIL_CONDITION       stage flag mask 0x200000 (bit(s) [21]) -- mission FAILS when set

=== level_23 (level 23, UsetupcontrolZ) ===
Objective 0  [briefing text id 8233]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x1000 (bit(s) [12])
   - FAIL_CONDITION       stage flag mask 0x2000 (bit(s) [13]) -- mission FAILS when set
Objective 1  [briefing text id 8234]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x4000 (bit(s) [14])
   - FAIL_CONDITION       stage flag mask 0x10000 (bit(s) [16]) -- mission FAILS when set
Objective 2  [briefing text id 8235]  min difficulty: Agent
   - DESTROY_OBJECT       tag 21 -> PROP
   - DESTROY_OBJECT       tag 22 -> PROP
   - DESTROY_OBJECT       tag 23 -> PROP
   - DESTROY_OBJECT       tag 24 -> PROP
   - DESTROY_OBJECT       tag 25 -> PROP
   - DESTROY_OBJECT       tag 26 -> PROP

=== level_24 (level 24, UsetuparchZ) ===
Objective 0  [briefing text id 2089]  min difficulty: Agent
   - ENTER_ROOM           ref @4=152 (room); runtime word @8=0x0
Objective 1  [briefing text id 2090]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x100 (bit(s) [8])
Objective 2  [briefing text id 2091]  min difficulty: Agent
   - COLLECT_OBJECT       tag 2 -> PROP
   - FAIL_CONDITION       stage flag mask 0x1000 (bit(s) [12]) -- mission FAILS when set
Objective 3  [briefing text id 2092]  min difficulty: Agent
   - ENTER_ROOM           ref @4=208 (room); runtime word @8=0x0
   - COMPLETE_CONDITION   stage flag mask 0x400 (bit(s) [10])
   - FAIL_CONDITION       stage flag mask 0x200 (bit(s) [9]) -- mission FAILS when set

=== level_25 (level 25, UsetuptraZ) ===
Objective 0  [briefing text id 36898]  min difficulty: Agent
   - DESTROY_OBJECT       tag 8 -> PROP
   - DESTROY_OBJECT       tag 9 -> PROP
   - DESTROY_OBJECT       tag 10 -> PROP
   - DESTROY_OBJECT       tag 11 -> PROP
   - DESTROY_OBJECT       tag 12 -> PROP
   - DESTROY_OBJECT       tag 13 -> PROP
Objective 1  [briefing text id 36899]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x8000 (bit(s) [15])
   - FAIL_CONDITION       stage flag mask 0x4000 (bit(s) [14]) -- mission FAILS when set
Objective 2  [briefing text id 36900]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x10000 (bit(s) [16])
Objective 3  [briefing text id 36901]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x20000 (bit(s) [17])
Objective 4  [briefing text id 36902]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x100000 (bit(s) [20])

=== level_26 (level 26, UsetupdestZ) ===
Objective 0  [briefing text id 13332]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x100 (bit(s) [8])
   - COMPLETE_CONDITION   stage flag mask 0x200 (bit(s) [9])
   - COMPLETE_CONDITION   stage flag mask 0x400 (bit(s) [10])
   - COMPLETE_CONDITION   stage flag mask 0x800 (bit(s) [11])
   - COMPLETE_CONDITION   stage flag mask 0x1000 (bit(s) [12])
   - FAIL_CONDITION       stage flag mask 0x100000 (bit(s) [20]) -- mission FAILS when set
Objective 1  [briefing text id 13333]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x10000 (bit(s) [16])
   - FAIL_CONDITION       stage flag mask 0x40000 (bit(s) [18]) -- mission FAILS when set
Objective 2  [briefing text id 13334]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x20000 (bit(s) [17])
   - FAIL_CONDITION       stage flag mask 0x80000 (bit(s) [19]) -- mission FAILS when set
Objective 3  [briefing text id 13335]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x4000 (bit(s) [14])
   - FAIL_CONDITION       stage flag mask 0x8000 (bit(s) [15]) -- mission FAILS when set

=== level_27 (level 27, UsetupsevbZ) ===
Objective 0  [briefing text id 29743]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x200 (bit(s) [9])
Objective 1  [briefing text id 29744]  min difficulty: Agent
   - FAIL_CONDITION       stage flag mask 0x1000 (bit(s) [12]) -- mission FAILS when set
   - COLLECT_OBJECT       tag 2 -> PROP
Objective 2  [briefing text id 29745]  min difficulty: Agent
   - DESTROY_OBJECT       tag 28 -> CCTV
   - DESTROY_OBJECT       tag 29 -> CCTV
   - DESTROY_OBJECT       tag 30 -> CCTV
   - DESTROY_OBJECT       tag 31 -> CCTV
   - DESTROY_OBJECT       tag 32 -> CCTV
   - DESTROY_OBJECT       tag 33 -> CCTV
Objective 3  [briefing text id 29746]  min difficulty: Agent
   - COLLECT_OBJECT       tag 34 -> PROP
Objective 4  [briefing text id 29747]  min difficulty: Agent
   - FAIL_CONDITION       stage flag mask 0x100 (bit(s) [8]) -- mission FAILS when set
   - COMPLETE_CONDITION   stage flag mask 0x800 (bit(s) [11])
   - ENTER_ROOM           ref @4=51 (room); runtime word @8=0x0

=== level_28 (level 28, UsetupaztZ) ===
Objective 0  [briefing text id 5141]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x1000 (bit(s) [12])
Objective 1  [briefing text id 5142]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x8000 (bit(s) [15])

=== level_29 (level 29, UsetuppeteZ) ===
Objective 0  [briefing text id 25627]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x20000 (bit(s) [17])
   - FAIL_CONDITION       stage flag mask 0x40000 (bit(s) [18]) -- mission FAILS when set
Objective 1  [briefing text id 25625]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x4000 (bit(s) [14])
   - FAIL_CONDITION       stage flag mask 0x8000 (bit(s) [15]) -- mission FAILS when set
Objective 2  [briefing text id 25626]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x4000 (bit(s) [14])
   - FAIL_CONDITION       stage flag mask 0x1000 (bit(s) [12]) -- mission FAILS when set

=== level_30 (level 30, UsetupdepoZ) ===
Objective 0  [briefing text id 12303]  min difficulty: Agent
   - DESTROY_OBJECT       tag 10 -> PROP
   - DESTROY_OBJECT       tag 11 -> PROP
   - DESTROY_OBJECT       tag 12 -> PROP
   - DESTROY_OBJECT       tag 13 -> PROP
   - DESTROY_OBJECT       tag 14 -> PROP
   - DESTROY_OBJECT       tag 15 -> PROP
   - DESTROY_OBJECT       tag 16 -> PROP
   - DESTROY_OBJECT       tag 17 -> PROP
   - DESTROY_OBJECT       tag 18 -> PROP
   - DESTROY_OBJECT       tag 19 -> PROP
   - DESTROY_OBJECT       tag 20 -> PROP
   - DESTROY_OBJECT       tag 21 -> PROP
   - DESTROY_OBJECT       tag 22 -> PROP
   - DESTROY_OBJECT       tag 23 -> PROP
   - DESTROY_OBJECT       tag 24 -> PROP
   - DESTROY_OBJECT       tag 25 -> PROP
   - DESTROY_OBJECT       tag 26 -> PROP
Objective 1  [briefing text id 12304]  min difficulty: Agent
   - DESTROY_OBJECT       tag 1 -> MULTI_MONITOR
   - DESTROY_OBJECT       tag 2 -> PROP
   - DESTROY_OBJECT       tag 3 -> PROP
Objective 2  [briefing text id 12305]  min difficulty: Agent
   - COLLECT_OBJECT       tag 0 -> KEY
Objective 3  [briefing text id 12306]  min difficulty: Agent
   - COLLECT_OBJECT       tag 5 -> PROP
Objective 4  [briefing text id 12307]  min difficulty: Agent
   - COMPLETE_CONDITION   stage flag mask 0x100 (bit(s) [8])
UsetuprefZ not in filelist
