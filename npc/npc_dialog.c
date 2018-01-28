
/* npc.c
 * Code for npc dialog and activities */

#include "npc.h"

int npc_match_reply(dbref npc, dbref player, const char *reply)
{
  char node[BUFFER_LEN];
  char buff[BUFFER_LEN];
  char list[BUFFER_LEN];
  char *bp, *np, *lp, *rp;
  
  lp = list;
  rp = reply;
  
  /* run through reply and copy words to list */
  
  
  if (!RealGoodObject(npc) || !RealGoodObject(player))
    return 0;
  
  if (!IsNPC(npc))
    return 0;
  
  strcpy(node, npc_get_player_node(npc, player));
  
  bp = buff;
  safe_str("DIALOG`", buff, &bp);
  safe_str(node, buff, &bp);
  safe_str("`REPLY`*", buff, &bp);

  return 1;  
}


/* find out which dialog node an player is on */
const char *npc_get_player_node(dbref npc, dbref player)
{
  static char node[BUFFER_LEN];
  char buff[BUFFER_LEN];
  char *bp, *np;
  time_t ntime;
  ATTR *a;
  
  if (!RealGoodObject(npc) || !RealGoodObject(player))
    return NULL;

  if (!IsNPC(npc))
    return NULL;

  bp = buff;
  safe_str("_DIALOG`", buff, &bp);
  safe_dbref(player, buff, &bp);
  *bp = '\0';

  a = atr_get_noparent(npc, buff);
  if (!a) {
    /* there was no attribute set, set node to default */
    np = node;
    safe_str(NPC_NODE_DEFAULT, node, &np);
    *np = '\0';
    npc_set_player_node(npc, player, node);
    return node;
  }

  /* read the attribute so we can get the node and timestamp */
  bp = buff;
  safe_str(atr_value(a), buff, &bp);
  *bp = '\0';
  
  ntime = parse_int(buff, &bp, 10);
  if (!bp || *bp != ':') {
    /* there's no valid node with timestamp attached, set node to default */
    np = node;
    safe_str(NPC_NODE_DEFAULT, node, &np);
    *np = '\0';
    npc_set_player_node(npc, player, node);
    return node;
  }
  
  if ((mudtime - ntime) > NPC_TIMEOUT) {
    /* the timestamp was blank or too old, set node to default */
    np = node;
    safe_str(NPC_NODE_DEFAULT, node, &np);
    *np = '\0';
    npc_set_player_node(npc, player, node);
    return node;
  }
  
  bp++;
  if (bp > buff+BUFFER_LEN-1 || *bp == '\0') {
    /* there's no valid node, set node to default */
    np = node;
    safe_str(NPC_NODE_DEFAULT, node, &np);
    *np = '\0';
    npc_set_player_node(npc, player, node);
    return node;
  }
  
  np = node;
  safe_str(bp, node, &np);
  *np = '\0';
  
  /* timestamp checks out, return the node */
  return node;
}

/* set a player's dialog node on an npc */
void npc_set_player_node(dbref npc, dbref player, const char *node)
{
  char atr[BUFFER_LEN];
  char buff[BUFFER_LEN];
  char *bp;
  
  if (!RealGoodObject(npc) || !RealGoodObject(player))
    return;
  
  bp = atr;
  safe_str("_DIALOG`", atr, &bp);
  safe_dbref(player, atr, &bp);
  *bp = '\0';
  
  /* invalid node means to clear the attribute */
  if (!node || !*node) {
    atr_clr(npc, atr, npc);
    return;
  }
  
  /* set the node state and current time */
  bp = buff;
  safe_str(unparse_integer(mudtime), buff, &bp);
  safe_chr(':', buff, &bp);
  safe_str(node, buff, &bp);
  *bp = '\0';
  
  atr_add(npc, atr, buff, npc, 0);
  return;  
}




















