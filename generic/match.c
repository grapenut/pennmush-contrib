/**
 * \file match.c
 *
 * \brief Matching of object names.
 *
 * \verbatim
 * These are the PennMUSH name-matching routines, fully re-entrant.
 *  match_result_relative(who,where,name,type,flags) return match, AMBIGUOUS or
 * NOTHING
 *  match_result(who,name,type,flags) - return match, AMBIGUOUS, or NOTHING
 *  noisy_match_result(who,name,type,flags) - return match or NOTHING,
 *      and notify player on failures
 *  last_match_result(who,name,type,flags) - return match or NOTHING,
 *      and return the last match found in ambiguous situations
 *
 *  match_result_internal() does the legwork for all of the above.
 *
 * who = dbref of player to match for
 * where = dbref of object to match relative to. For all functions which don't
 * take a 'where' arg, use 'who'.
 * name = string to match on
 * type = preferred type(s) of match (TYPE_THING, etc.) or NOTYPE
 * flags = a set of bits indicating what kind of matching to do
 *
 * flags are defined in match.h, but here they are for reference:
 * MAT_CHECK_KEYS       - prefer objects whose Basic lock 'who' passes
 * MAT_GLOBAL           - match in master room
 * MAT_REMOTES          - match ZMR exits
 * MAT_NEAR             - match things nearby
 * MAT_CONTROL          - only match objects 'who' controls
 * MAT_ME               - match "me"
 * MAT_HERE             - match "here"
 * MAT_ABSOLUTE         - match any <#dbref>
 * MAT_PMATCH           - match <playerName> or *<playerName>
 * MAT_PLAYER           - match *<playerName>
 * MAT_NEIGHBOR         - match something in 'where's location
 * MAT_POSSESSION       - match something in 'where's inventory
 * MAT_EXIT             - match an exit in 'where's location
 * MAT_CARRIED_EXIT     - match an exit in the room 'where'
 * MAT_CONTAINER        - match the name of 'where's location
 * MAT_REMOTE_CONTENTS  - matches the same as MAT_POSSESSION
 * MAT_ENGLISH          - match natural english 'my 2nd flower'
 * MAT_TYPE             - match only objects of the given type(s)
 * MAT_EXACT            - only do full-name matching, no partial names
 * MAT_EVERYTHING       - me,here,absolute,player,neighbor,possession,exit
 * MAT_NEARBY           - everything,near
 * MAT_OBJECTS          - me,absolute,player,neigbor,possession
 * MAT_NEAR_THINGS      - objects,near
 * MAT_REMOTE           - absolute,player,remote_contents,exit,remotes
 * MAT_LIMITED          - absolute,player,neighbor
 * MAT_CONTENTS         - only match objects located inside 'where'
 * MAT_OBJ_CONTENTS     - possession,player,absolute,english,contents
 * \endverbatim
 */

#include "copyrite.h"
#include "match.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "attrib.h"
#include "case.h"
#include "conf.h"
#include "dbdefs.h"
#include "externs.h"
#include "flags.h"
#include "mushdb.h"
#include "mymalloc.h"
#include "notify.h"
#include "parse.h"
#include "strutil.h"

struct match_context
{
  dbref match;     /* object we're currently checking for a match */
  dbref bestmatch; /* the best match we've found so bar */
  dbref abs;       /* try to match xname as a dbref/objid */
  dbref who;       /* */
  dbref where;     /* */
  long flags;      /* */
  int type;        /* */
  int final;       /* the Xth object we want, with english matching (5th foo) */
  int curr;        /* the number of matches found so far, when 'final' is used */
  int nocontrol;   /* set when we've matched an object,
                      but don't control it and MAT_CONTROL is given */
  int right_type;  /* number of objects of preferred type found,
                      when we have a type but MAT_TYPE isn't given */
  int exact;       /* set to 1 when we've found an exact match, not just a partial one */
  int done;        /* set to 1 when we're using final, and have found the Xth object */
  char *name;      /* name contains the object name searched for, 
                      after english matching tokens are stripped from xname */
};

static int parse_english(char **name, long *flags);
static int matched(int full, struct match_context *mc);
static int match_obj_list(dbref start, struct match_context *mc);
static int match_attr_list(dbref start, struct match_context *mc);
static dbref match_player(dbref who, const char *name, int partial);
extern int check_alias(const char *command, const char *list); /* game.c */
static dbref choose_thing(const dbref who, const int preferred_type, long flags,
                          dbref thing1, dbref thing2);
static dbref match_result_internal(dbref who, dbref where, const char *xname,
                                   int type, long flags);

dbref
noisy_match_result(const dbref who, const char *name, const int type,
                   const long flags)
{
  dbref match;

  match = match_result(who, name, type, flags | MAT_NOISY);
  if (!GoodObject(match))
    return NOTHING;
  else
    return match;
}

dbref
last_match_result(const dbref who, const char *name, const int type,
                  const long flags)
{
  return match_result(who, name, type, flags | MAT_LAST);
}

dbref
match_controlled(dbref player, const char *name)
{
  return noisy_match_result(player, name, NOTYPE, MAT_EVERYTHING | MAT_CONTROL);
}

/* The real work. Here's the spec:
 * str  --> "me"
 *      --> "here"
 *      --> "#dbref"
 *      --> "*player"
 *      --> adj-phrase name
 *      --> name
 * adj-phrase --> adj
 *            --> adj count
 *            --> count
 * adj  --> "my", "me" (restrict match to inventory)
 *      --> "here", "this", "this here" (restrict match to neighbor objects)
 *      --> "toward" (restrict match to exits)
 * count --> 1st, 21st, etc.
 *       --> 2nd, 22nd, etc.
 *       --> 3rd, 23rd, etc.
 *       --> 4th, 10th, etc.
 * name --> exit_alias
 *      --> full_obj_name
 *      --> partial_obj_name
 *
 * 1. Look for exact matches and return immediately:
 *  a. "me" if requested
 *  b. "here" if requested
 *  c. #dbref, possibly with a control check
 *  d. *player
 * 2. Parse for adj-phrases and restrict further matching and/or
 *    remember the object count
 * 3. Look for matches (remote contents, neighbor, inventory, exits,
 *    containers, carried exits)
 *  a. If we don't have an object count, collect the number of exact
 *     and partial matches and the best partial match.
 *  b. If we do have an object count, collect the nth exact match
 *     and the nth match (exact or partial). number of matches is always
 *     0 or 1.
 * 4. Make decisions
 *  a. If we got a single exact match, return it
 *  b. If we got multiple exact matches, complain
 *  c. If we got no exact matches, but a single partial match, return it
 *  d. If we got multiple partial matches, complain
 *  e. If we got no matches, complain
 */

#define MATCH_CONTROLS (!(mc->flags & MAT_CONTROL) || controls(mc->who, mc->match))

#define MATCH_TYPE ((mc->type & Typeof(mc->match)) ? 1 : ((mc->flags & MAT_TYPE) ? 0 : -1))

#define MATCH_CONTENTS (!(mc->flags & MAT_CONTENTS) || (Location(mc->match) == mc->where))

#define BEST_MATCH choose_thing(mc->who, mc->type, mc->flags, mc->bestmatch, mc->match)

#define MATCH_GENERIC(container)               \
    {                                          \
      if (match_attr_list(container, mc))      \
        break;                                 \
    }

#define MATCH_LIST(start)                     \
    {                                         \
      result = match_obj_list(start, mc);     \
      if (!result)                            \
        continue;                             \
      else if (result == 1)                   \
        break;                                \
    }

#define MATCHED(full)                         \
    {                                         \
      if (matched(full, mc))                  \
        break;                                \
      else                                    \
        continue;                             \
    }

/* matched() is called from inside match_obj_list() and match_attr_list(). Full is 1 if the
  match was full/exact, and 0 if it was partial.
  Returns 0 if matching should continue, or returns 1 if we are done. */
static int matched(int full, struct match_context *mc)
{
  if (!mc)
    return 0;
    
  if (!MATCH_CONTROLS) {
    /* Found a match object, but we lack necessary control */
    mc->nocontrol = 1;
    return 0;
  }
  if (!mc->final) {
    mc->bestmatch = BEST_MATCH;
    if (mc->bestmatch != mc->match) {
      /* Previously matched item won over due to type, @lock, etc, checks */
      return 0;
    }
    if (full) {
      if (mc->exact) {
        /* Another exact match */
        mc->curr++;
      } else {
        /* Ignore any previous partial matches now we have an exact match */
        mc->exact = 1;
        mc->curr = 1;
        mc->right_type = 0;
      }
    } else {
      /* Another partial match */
      mc->curr++;
    }
    if (mc->type != NOTYPE && (Typeof(mc->bestmatch) & mc->type))
      mc->right_type++;
  } else {
    mc->curr++;
    if (mc->curr == mc->final) {
      /* we've successfully found the Nth item */
      mc->bestmatch = mc->match;
      mc->done = 1;
      return 1;
    }
  }
  
  return 0;
}

static int match_attr_helper(dbref player __attribute__((__unused__)) , dbref thing __attribute__((__unused__)), 
                             dbref parent __attribute__((__unused__)), char const *pattern __attribute__((__unused__)), 
                             ATTR *atr, void *args)
{
  int num;
  dbref obj;
  char *s, *p;
  struct match_context *mc = (struct match_context *) args;

  if (!mc || mc->done)
    return 0;

  s = p = mush_strdup(AL_NAME(atr), "generic.atrname");
  strsep(&p, "`");
  obj = parse_dbref(p);

  mush_free(s, "generic.atrname");
  
  if (!RealGoodObject(obj) || !has_flag_by_name(obj, "GENERIC", TYPE_THING))
    return 0;
  
  num = parse_integer(atr_value(atr));
  if (num <= 0) {
    return 0;
  }
  
  mc->match = obj;

  if (!MATCH_TYPE) {
    /* Exact-type match required, but failed */
    return 0;
  } else if (mc->match == mc->abs) {
    /* absolute dbref match in list */
    return matched(1, mc);
  } else if (!can_interact(mc->match, mc->who, INTERACT_MATCH, NULL)) {
    /* Not allowed to match this object */
    return 0;
  } else if (match_aliases(mc->match, mc->name) ||
             (!IsExit(mc->match) && !strcasecmp(Name(mc->match), mc->name))) {
    /* exact name match */
    return matched(1, mc);
  } else if (!(mc->flags & MAT_EXACT) && (!mc->exact || !GoodObject(mc->bestmatch)) &&
             !IsExit(mc->match) && string_match(Name(mc->match), mc->name)) {
    /* partial name match */
    return matched(0, mc);
  }

  return 1;
}

static int match_attr_list(dbref obj, struct match_context *mc)
{
  void *args = (void *) mc;
  
  if (!mc)
    return 0;
  
  if (mc->done)
    return 1;
  
  atr_iter_get_parent(GOD, obj, "GENERIC`*", 0, 0, &match_attr_helper, args);
  
  if (mc->done)
    return 1;
  
  return 0;
}

/* match_obj_list() is called from inside the match_result() function. start is the
   dbref to begin matching at (we loop through using DOLIST()).
   Returns 0 if we should continue matching, or returns 1 if we are done. */
static int match_obj_list(dbref start, struct match_context *mc)
{
  if (mc->done)
    return 1; /* already found the Nth object we needed */
  mc->match = start;
  DOLIST(mc->match, mc->match)
  {
    if (!MATCH_TYPE) {
      /* Exact-type match required, but failed */
      continue;
    } else if (mc->match == mc->abs) {
      /* absolute dbref match in list */
      MATCHED(1);
    } else if (!can_interact(mc->match, mc->who, INTERACT_MATCH, NULL)) {
      /* Not allowed to match this object */
      continue;
    } else if (match_aliases(mc->match, mc->name) ||
               (!IsExit(mc->match) && !strcasecmp(Name(mc->match), mc->name))) {
      /* exact name match */
      MATCHED(1);
    } else if (!(mc->flags & MAT_EXACT) && (!mc->exact || !GoodObject(mc->bestmatch)) &&
               !IsExit(mc->match) && string_match(Name(mc->match), mc->name)) {
      /* partial name match */
      MATCHED(0);
    }
  }
  
  return 2;
}

static dbref
choose_thing(const dbref who, const int preferred_type, long flags,
             dbref thing1, dbref thing2)
{
  int key;
  /* If there's only one valid thing, return it */
  /* Rather convoluted to ensure we always return AMBIGUOUS, not NOTHING, if we
   * have one of each */
  /* (Apologies to Theodor Geisel) */
  if (!GoodObject(thing1) && !GoodObject(thing2)) {
    if (thing1 == NOTHING)
      return thing2;
    else
      return thing1;
  } else if (!GoodObject(thing1)) {
    return thing2;
  } else if (!GoodObject(thing2)) {
    return thing1;
  }

  /* If a type is given, and only one thing is of that type, return it */
  if (preferred_type != NOTYPE) {
    if (Typeof(thing1) & preferred_type) {
      if (!(Typeof(thing2) & preferred_type)) {
        return thing1;
      }
    } else if (Typeof(thing2) & preferred_type) {
      return thing2;
    }
  }

  if (flags & MAT_CHECK_KEYS) {
    key = could_doit(who, thing1, NULL);
    if (!key && could_doit(who, thing2, NULL)) {
      return thing2;
    } else if (key && !could_doit(who, thing2, NULL)) {
      return thing1;
    }
  }
  /* No luck. Return last match */
  return thing2;
}

static dbref
match_player(dbref who, const char *name, int partial)
{
  dbref match;

  if (*name == LOOKUP_TOKEN) {
    name++;
  }

  while (isspace(*name)) {
    name++;
  }

  match = lookup_player(name);
  if (match != NOTHING) {
    return match;
  }
  return (GoodObject(who) && partial ? visible_short_page(who, name) : NOTHING);
}

int
match_aliases(dbref match, const char *name)
{

  if (!IsPlayer(match) && !IsExit(match)) {
    return 0;
  }

  if (IsExit(match) && check_alias(name, Name(match)))
    return 1;
  else {
    char tbuf1[BUFFER_LEN];
    ATTR *a = atr_get_noparent(match, "ALIAS");
    if (!a)
      return 0;
    mush_strncpy(tbuf1, atr_value(a), BUFFER_LEN);
    return check_alias(name, tbuf1);
  }
}

dbref
match_result(dbref who, const char *xname, int type, long flags)
{
  return match_result_internal(who, who, xname, type, flags);
}

dbref
match_result_relative(dbref who, dbref where, const char *xname, int type,
                      long flags)
{
  return match_result_internal(who, where, xname, type, flags);
}

/* The object 'who' is trying to find something called 'xname' relative to the
 * object 'where'.
 * In most cases, 'who' and 'where' will be the same object. */
static dbref
match_result_internal(dbref who, dbref where, const char *xname, int type,
                      long flags)
{
  struct match_context context;
  struct match_context *mc = &context;
  
  int result;
  dbref loc;       /* location of 'where' */
  int goodwhere = RealGoodObject(where);
  char *name, *sname; /* name contains the object name searched for, after
                         english matching tokens are stripped from xname */

  mc->who = who;
  mc->where = where;
  mc->type = type;
  mc->flags = flags;
  mc->bestmatch = NOTHING;
  mc->abs = parse_objid(xname);
  mc->final = 0;
  mc->curr = 0;
  mc->nocontrol = 0;
  mc->right_type = 0;
  mc->exact = 0;
  mc->done = 0;

  if (!goodwhere)
    loc = NOTHING;
  else if (IsRoom(where))
    loc = where;
  else if (IsExit(where))
    loc = Source(where);
  else
    loc = Location(where);

  if (((flags & MAT_NEAR) && !goodwhere) ||
      ((flags & MAT_CONTENTS) && !goodwhere)) {
    /* It can't be nearby/in where's contents if where is invalid */
    if ((flags & MAT_NOISY) && GoodObject(who)) {
      notify(who, T("I can't see that here."));
    }
    return NOTHING;
  }

  /* match "me" */
  mc->match = where;
  if (goodwhere && MATCH_TYPE && (flags & MAT_ME) && !(flags & MAT_CONTENTS) &&
      !strcasecmp(xname, "me")) {
    if (MATCH_CONTROLS) {
      return mc->match;
    } else {
      mc->nocontrol = 1;
    }
  }

  /* match "here" */
  mc->match = (goodwhere ? (IsRoom(where) ? NOTHING : Location(where)) : NOTHING);
  if ((flags & MAT_HERE) && !(flags & MAT_CONTENTS) &&
      !strcasecmp(xname, "here") && GoodObject(mc->match) && MATCH_TYPE) {
    if (MATCH_CONTROLS) {
      return mc->match;
    } else {
      mc->nocontrol = 1;
    }
  }

  /* match *<player>, or <player> */
  if (((flags & MAT_PMATCH) ||
       ((flags & MAT_PLAYER) && *xname == LOOKUP_TOKEN)) &&
      ((type & TYPE_PLAYER) || !(flags & MAT_TYPE))) {
    mc->match = match_player(who, xname, !(flags & MAT_EXACT));
    if (MATCH_CONTENTS) {
      if (GoodObject(mc->match)) {
        if (!(flags & MAT_NEAR) || Long_Fingers(who) ||
            (nearby(who, mc->match) || controls(who, mc->match))) {
          if (MATCH_CONTROLS) {
            return mc->match;
          } else {
            mc->nocontrol = 1;
          }
        }
      } else {
        mc->bestmatch = BEST_MATCH;
      }
    }
  }

  /* dbref match */
  mc->match = mc->abs;
  if (RealGoodObject(mc->match) && (flags & MAT_ABSOLUTE) && MATCH_TYPE &&
      MATCH_CONTENTS) {
    if (!(flags & MAT_NEAR) || Long_Fingers(who) ||
        (nearby(who, mc->match) || controls(who, mc->match))) {
      /* valid dbref match */
      if (MATCH_CONTROLS) {
        return mc->match;
      } else {
        mc->nocontrol = 1;
      }
    }
  }

  sname = name = mush_strdup(xname, "mri.string");
  if (flags & MAT_ENGLISH) {
    /* English-style matching */
    mc->final = parse_english(&name, &flags);
  }
  mc->name = name;
  mc->flags = flags;

  while (1) {
    if (goodwhere && ((flags & (MAT_POSSESSION | MAT_REMOTE_CONTENTS)))) {
      MATCH_LIST(Contents(where));
      MATCH_GENERIC(where);
    }
    if (GoodObject(loc) && (flags & MAT_NEIGHBOR) && !(flags & MAT_CONTENTS) &&
        loc != where) {
      MATCH_LIST(Contents(loc));
      MATCH_GENERIC(loc);
    }
    if ((type & TYPE_EXIT) || !(flags & MAT_TYPE)) {
      if (GoodObject(loc) && IsRoom(loc) && (flags & MAT_EXIT)) {
        if ((flags & MAT_REMOTES) && !(flags & (MAT_NEAR | MAT_CONTENTS)) &&
            GoodObject(Zone(loc)) && IsRoom(Zone(loc))) {
          MATCH_LIST(Exits(Zone(loc)));
        }
        if ((flags & MAT_GLOBAL) && !(flags & (MAT_NEAR | MAT_CONTENTS))) {
          MATCH_LIST(Exits(MASTER_ROOM));
        }
        if (GoodObject(loc) && IsRoom(loc)) {
          MATCH_LIST(Exits(loc));
        }
      }
    }
    if ((flags & MAT_CONTAINER) && !(flags & MAT_CONTENTS) && goodwhere) {
      MATCH_LIST(loc);
    }
    if ((type & TYPE_EXIT) || !(flags & MAT_TYPE)) {
      if ((flags & MAT_CARRIED_EXIT) && goodwhere && IsRoom(where) &&
          ((loc != where) || !(flags & MAT_EXIT))) {
        MATCH_LIST(Exits(where));
      }
    }
    break;
  }

  if (!GoodObject(mc->bestmatch) && mc->final) {
    /* we never found the Nth item */
    mc->bestmatch = NOTHING;
  } else if (!mc->final && mc->curr > 1) {
    /* If we had a preferred type, and only found 1 of that type, give that,
     * otherwise ambiguous */
    if (mc->right_type != 1 && !(flags & MAT_LAST)) {
      mc->bestmatch = AMBIGUOUS;
    }
  }

  if (!GoodObject(mc->bestmatch) && (flags & MAT_NOISY) && GoodObject(who)) {
    /* give error message */
    if (mc->bestmatch == AMBIGUOUS) {
      notify(who, T("I don't know which one you mean!"));
    } else if (mc->nocontrol) {
      notify(who, T("Permission denied."));
    } else {
      notify(who, T("I can't see that here."));
    }
  }

  mush_free(sname, "mri.string");

  return mc->bestmatch;
}

/*
 * adj-phrase --> adj
 *            --> adj count
 *            --> count
 * adj  --> "my", "me" (restrict match to inventory)
 *      --> "here", "this", "this here" (restrict match to neighbor objects)
 *      --> "toward" (restrict match to exits)
 * count --> 1st, 21st, etc.
 *       --> 2nd, 22nd, etc.
 *       --> 3rd, 23rd, etc.
 *       --> 4th, 10th, etc.
 *
 * We return the count, we position the pointer at the end of the adj-phrase
 * (or at the beginning, if we fail), and we modify the flags if there
 * are restrictions
 */
static int
parse_english(char **name, long *flags)
{
  int saveflags = *flags;
  char *savename = *name;
  char *mname;
  char *e;
  int count = 0;

  /* Handle restriction adjectives first */
  if (*flags & MAT_NEIGHBOR) {
    if (!strncasecmp(*name, "this here ", 10)) {
      *name += 10;
      *flags &= ~(MAT_POSSESSION | MAT_EXIT);
    } else if (!strncasecmp(*name, "here ", 5) ||
               !strncasecmp(*name, "this ", 5)) {
      *name += 5;
      *flags &=
        ~(MAT_POSSESSION | MAT_EXIT | MAT_REMOTE_CONTENTS | MAT_CONTAINER);
    }
  }
  if ((*flags & MAT_POSSESSION) &&
      (!strncasecmp(*name, "my ", 3) || !strncasecmp(*name, "me ", 3))) {
    *name += 3;
    *flags &= ~(MAT_NEIGHBOR | MAT_EXIT | MAT_CONTAINER | MAT_REMOTE_CONTENTS);
  }
  if ((*flags & (MAT_EXIT | MAT_CARRIED_EXIT)) &&
      (!strncasecmp(*name, "toward ", 7))) {
    *name += 7;
    *flags &=
      ~(MAT_NEIGHBOR | MAT_POSSESSION | MAT_CONTAINER | MAT_REMOTE_CONTENTS);
  }

  while (**name == ' ')
    (*name)++;

  /* If the name was just 'toward' (with no object name), reset
   * everything and press on.
   */
  if (!**name) {
    *name = savename;
    *flags = saveflags;
    return 0;
  }

  /* Handle count adjectives */
  if (!isdigit(**name)) {
    /* Quick exit */
    return 0;
  }
  mname = strchr(*name, ' ');
  if (!mname) {
    /* Quick exit - count without a noun */
    return 0;
  }
  /* Ok, let's see if we can get a count adjective */
  savename = *name;
  *mname = '\0';
  count = strtoul(*name, &e, 10);
  if (e && *e) {
    if (count < 1) {
      count = -1;
    } else if ((count > 10) && (count < 14)) {
      if (strcasecmp(e, "th"))
        count = -1;
    } else if ((count % 10) == 1) {
      if (strcasecmp(e, "st"))
        count = -1;
    } else if ((count % 10) == 2) {
      if (strcasecmp(e, "nd"))
        count = -1;
    } else if ((count % 10) == 3) {
      if (strcasecmp(e, "rd"))
        count = -1;
    } else if (strcasecmp(e, "th")) {
      count = -1;
    }
  } else
    count = -1;
  *mname = ' ';
  if (count < 0) {
    /* An error (like '0th' or '12nd') - this wasn't really a count
     * adjective. Reset and press on. */
    *name = savename;
    return 0;
  }
  /* We've got a count adjective */
  *name = mname + 1;
  while (**name == ' ')
    (*name)++;
  return count;
}
