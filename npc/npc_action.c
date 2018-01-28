
/* npc.c
 * sequence actions for npcs, including movement and pathfinding */

#include "npc.h"

typedef struct DB_NODE dbnode;

struct DB_NODE {
  dbnode *ptr;
  dbref dir;
  dbref loc;
};

/* 
 * implement pathfinding algorithm, return list of exits from start to dest
 * while there are rooms on the frontier
 *   visit the next item from the frontier
 *   if this is our destination, stop and build the path string
 *   else go through each of the exits and add the destination to the frontier
 */
 
const char *npc_findpath(dbref player, dbref start, dbref stop)
{
  static char buff[BUFFER_LEN];
  char *bp;
  int i;
  int num_visited, num_frontier, cur_frontier;
  dbnode frontier[NPC_MAX_NODES];
  dbnode visited[NPC_MAX_NODES];
  dbnode *vp, *fp, *cur, *last;
  dbref dest, thing;
  int num_skips, has_visited, found_path;
  
  bp = buff;
  
  /* make sure we have a valid start and stop */
  if (!RealGoodObject(start) || !IsRoom(start))
  {
    safe_str("#-1 INVALID START", buff, &bp);
    *bp = '\0';
    return buff;
  }
  
  if (!RealGoodObject(stop) || !IsRoom(stop))
  {
    safe_str("#-1 INVALID STOP", buff, &bp);
    *bp = '\0';
    return buff;
  }
  
  if (start == stop)
  {
    safe_str("#-1 SAME LOCATION", buff, &bp);
    *bp = '\0';
    return buff;
  }
  
  if (!RealGoodObject(player))
  {
    safe_str("#-1 INVALID PLAYER", buff, &bp);
    *bp = '\0';
    return buff;
  }
  
  fp = frontier;
  num_frontier = 1;
  cur_frontier = 0;
  
  vp = visited;
  num_visited = 0;
  
  fp->ptr = NULL;
  fp->dir = NOTHING;
  fp->loc = start;
  
  found_path = 0;
  last = NULL;
  
  /* continue processing the frontier queue until it is empty */
  while (cur_frontier < num_frontier)
  {
    /* pop the current frontier off the queue */
    cur = &(frontier[cur_frontier++]);
    
    if (!RealGoodObject(cur->loc) || !IsRoom(cur->loc))
      continue;
    
    /* add it to the list of visited rooms */
    if (num_visited >= NPC_MAX_NODES)
    {
      safe_str("#-1 VISIT MEMORY EXHAUSTED", buff, &bp);
      *bp = '\0';
      return buff;
    }
    vp = &(visited[num_visited++]);
    vp->loc = cur->loc;
    vp->ptr = cur->ptr;
    vp->dir = cur->dir;
    
    /* iterate list of exits and add destinations to frontier */
    DOLIST_VISIBLE(thing, Exits(vp->loc), player)
    {
      dest = Destination(thing);
      if (!RealGoodObject(dest) || !IsRoom(dest))
        continue;
      
      /* make sure player can go through the exit */
      if (!could_doit(player, thing, NULL))
        continue;
      
      /* check if we have already visited this room, skip it if so */
      has_visited = 0;
      for (i = 0; i < num_visited; ++i)
      {
        if (dest == visited[i].loc)
        {
          has_visited = 1;
          break;
        }
      }
      for (i = cur_frontier; i < num_frontier; ++i)
      {
        if (dest == frontier[i].loc)
        {
          has_visited = 1;
          break;
        }
      }
      if (has_visited)
      {
        continue;
      }
      
      /* check if we found our destination */
      if (dest == stop)
      {
        /* we found the best path to the end */
        /* add it to visited and exit early! */
        if (num_visited >= NPC_MAX_NODES)
        {
          safe_str("#-1 LAST MEMORY EXHAUSTED", buff, &bp);
          *bp = '\0';
          return buff;
        }
        last = &(visited[num_visited++]);
        last->loc = dest;
        last->ptr = vp;
        last->dir = thing;
        
        found_path = 1;
        break;
      }
      
      /* just another bump in the road, push destination onto frontier */
      if (num_frontier >= NPC_MAX_NODES)
      {
        safe_str("#-1 FRONTIER MEMORY EXHAUSTED", buff, &bp);
        *bp = '\0';
        return buff;
      }
      fp = &(frontier[num_frontier++]);
      fp->loc = dest;
      fp->ptr = vp;
      fp->dir = thing;
    }
    
    /* we found the path already, no need to process the frontier any further */
    if (found_path)
      break;
  
  }
  
  if (found_path)
  {
    /* walk the path backwards, cache exits reusing frontier */
    num_frontier = 0;
    vp = last;
    for (cur = last->ptr; cur && last; last = cur, cur = cur->ptr)
    {
      fp = &(frontier[num_frontier++]);
      fp->dir = last->dir;
    }
    
    /* build the path string using cached exits */
    for (i = num_frontier-1; i >= 0; --i)
    {
      cur = &(frontier[i]);
    
      if (bp != buff)
        safe_chr(' ', buff, &bp);
      safe_str(unparse_dbref(cur->dir), buff, &bp);
    }
  }
  else
  {
    safe_str("#-1 PATH NOT FOUND", buff, &bp);
    *bp = '\0';
    return buff;
  }
  
  *bp = '\0';
  return buff;

}








