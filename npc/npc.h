#ifndef __NPC_H
#define __NPC_H

#include "conf.h"
#include "externs.h"
#include "strutil.h"
#include "attrib.h"
#include "parse.h"
#include "notify.h"


#define NPC_TIMEOUT	300
#define NPC_NODE_ERROR		-1
#define NPC_NODE_DEFAULT	0
#define NPC_MAX_NODES	512

#define STARTBUF	bp = buff
#define ENDBUF		*bp = '\0'

#define IsNPC(x) (has_flag_by_name(x, "NPC", NOTYPE))

/* NPC Dialog */
extern int npc_get_player_node(dbref, dbref);
extern void npc_set_player_node(dbref, dbref, int);

/* NPC Action Sequencing */
extern const char *npc_findpath(dbref, dbref, dbref);

#endif /* __NPC_H */
