
/* npc.c
 * Code for npc dialog and activities */

#include "npc.h"


int npc_match_reply(dbref npc, dbref player, 


/* find out which dialog node an player is on */
int npc_get_player_node(dbref npc, dbref player)
{
  ATTR *a;
  char buff[BUFFER_LEN];
  char *bp;
  time_t ntime;
  int n;
  
  if (!RealGoodObject(npc) || !RealGoodObject(player))
    return NPC_NODE_ERROR;

  if (!IsNPC(npc))
    return NPC_NODE_ERROR;

  STARTBUF;
  safe_str("NODE`", buff, &bp);
  safe_dbref(player, buff, &bp);
  ENDBUF;

  a = atr_get_noparent(npc, buff);
  if (!a) {
    /* there was no attribute set, set node to default */
    npc_set_player_node(npc, player, NPC_NODE_DEFAULT);
    return NPC_NODE_DEFAULT;
  }

  /* read the attribute so we can get the node and timestamp */
  STARTBUF;
  safe_str(atr_value(a), buff, &bp);
  ENDBUF;
  
  n = parse_int(buff, &bp, 10);
  if (*bp != ':') {
    /* there's no timestamp attached, set node to default */
    npc_set_player_node(npc, player, NPC_NODE_DEFAULT);
    return NPC_NODE_DEFAULT;
  }
  
  bp++;
  ntime = parse_int(bp, NULL, 10);
  if ((mudtime - ntime) > NPC_TIMEOUT) {
    /* the timestamp was blank or too old, set node to default */
    npc_set_player_node(npc, player, NPC_NODE_DEFAULT);
    return NPC_NODE_DEFAULT;
  }
  
  /* timestamp checks out, return the node */
  return n;
}

/* set a player's dialog node on an npc */
void npc_set_player_node(dbref npc, dbref player, int node)
{
  char buff[BUFFER_LEN];
  char buff2[BUFFER_LEN];
  char *bp;
  
  if (!RealGoodObject(npc) || !RealGoodObject(player))
    return;
  
  STARTBUF;
  safe_str("NODE`", buff, &bp);
  safe_dbref(player, buff, &bp);
  ENDBUF;
  
  /* invalid node means to clear the attribute */
  if (node < 0) {
    atr_clr(npc, buff, npc);
    return;
  }
  
  /* set the node state and current time */
  STARTBUF2;
  safe_str(unparse_integer(node), buff2, &bp);
  safe_chr(':', buff2, &bp);
  safe_str(unparse_integer(mudtime), buff2, &bp);
  ENDBUF;
  
  atr_add(npc, buff, buff2, npc, 0);
  return;  
}




















