/*
 * Copyright (C) 2003 Robert Kooima
 *
 * NEVERBALL is  free software; you can redistribute  it and/or modify
 * it under the  terms of the GNU General  Public License as published
 * by the Free  Software Foundation; either version 2  of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 */

#include "progress.h"
#include "config.h"
#include "demo.h"
#include "level.h"
#include "set.h"
#include "lang.h"
#include "score.h"
#include "audio.h"

#include "game_common.h"
#include "game_client.h"
#include "game_server.h"

#include <assert.h>

/*---------------------------------------------------------------------------*/

struct progress
{
    int balls;
    int score;
    int times;
};

static int replay = 0;

static int mode = MODE_NORMAL;

static struct level *level;
static struct level *next;

static int done  =  0;

static int bonus =  0;

static struct progress curr;
static struct progress prev;

/* Set stats. */

static int score_rank = 3;
static int times_rank = 3;

/* Level stats. */

static int status = GAME_NONE;

static int coins = 0;
static int timer = 0;

static int goal   = 0; /* Current goal value. */
static int goal_i = 0; /* Initial goal value. */

static int goal_e      = 0; /* Goal enabled flag                */
static int same_goal_e = 0; /* Reuse existing goal enabled flag */

static int time_rank = 3;
static int goal_rank = 3;
static int coin_rank = 3;

/*---------------------------------------------------------------------------*/

void progress_init(int m)
{
    mode  = m;
    bonus = 0;

    replay = 0;

    curr.balls = 2;
    curr.score = 0;
    curr.times = 0;

    prev = curr;

    score_rank = times_rank = 3;

    done  = 0;
}

static int init_level(void)
{
    demo_play_init(USER_REPLAY_FILE, level, mode,
                   level_time(level), level_goal(level),
                   goal_e, curr.score, curr.balls, curr.times);

    /*
     * Init both client and server, then process the first batch
     * of commands generated by the server to sync client to
     * server.
     */

    if (game_client_init(level_file(level)) &&
        game_server_init(level_file(level), level_time(level), goal_e))
    {
        game_client_sync(demo_fp);
        audio_music_fade_to(2.0f, level_song(level));
        return 1;
    }

    demo_play_stop();
    return 0;
}

int  progress_play(struct level *l)
{
    if (l && (level_opened(l) || config_cheat()))
    {
        level = l;

        next   = NULL;
        status = GAME_NONE;
        coins  = 0;
        timer  = 0;
        goal   = goal_i = level_goal(level);

        if (same_goal_e)
            same_goal_e = 0;
        else
            goal_e = (mode != MODE_CHALLENGE && level_completed(level) &&
                      config_get_d(CONFIG_LOCK_GOALS) == 0) || goal == 0;

        prev = curr;

        time_rank = goal_rank = coin_rank = 3;

        return init_level();
    }
    return 0;
}

void progress_step(void)
{
    if (goal > 0)
    {
        goal = goal_i - curr_coins();

        if (goal <= 0)
        {
            if (!replay)
                game_set_goal();

            goal = 0;
        }
    }
}

void progress_stat(int s)
{
    int i, dirty = 0;

    status = s;

    coins = curr_coins();
    timer = (level_time(level) == 0 ?
             curr_clock() :
             level_time(level) - curr_clock());

    switch (status)
    {
    case GAME_GOAL:

        for (i = curr.score + 1; i <= curr.score + coins; i++)
            if (progress_reward_ball(i))
                curr.balls++;

        curr.score += coins;
        curr.times += timer;

        dirty = level_score_update(level, timer, coins,
                                   &time_rank,
                                   goal == 0 ? &goal_rank : NULL,
                                   &coin_rank);

        if (!level_completed(level))
        {
            level_complete(level);
            dirty = 1;
        }

        /* Compute next level. */

        if (mode == MODE_CHALLENGE)
        {
            for (next = level->next;
                 next && level_bonus(next);
                 next = next->next)
            {
                if (!level_opened(next))
                {
                    level_open(next);
                    dirty = 1;
                    bonus++;
                }
            }
        }
        else
        {
            for (next = level->next;
                 next && level_bonus(next) && !level_opened(next);
                 next = next->next)
                /* Do nothing */;
        }

        /* Open next level or complete the set. */

        if (next)
        {
            level_open(next);
            dirty = 1;
        }
        else
            done = mode == MODE_CHALLENGE;

        break;

    case GAME_FALL:
        /* Fall through. */

    case GAME_TIME:
        for (next = level->next;
             next && !level_opened(next);
             next = next->next)
            /* Do nothing */;

        curr.times += timer;
        curr.balls -= 1;

        break;
    }

    if (dirty)
        set_store_hs();

    demo_play_stat(status, coins, timer);
}

void progress_stop(void)
{
    demo_play_stop();
}

void progress_exit(void)
{
    assert(done);

    if (set_score_update(curr.times, curr.score, &score_rank, &times_rank))
        set_store_hs();
}

int  progress_replay(const char *filename)
{
    if (demo_replay_init(filename, &goal, &mode,
                         &curr.balls,
                         &curr.score,
                         &curr.times))
    {
        goal_i = goal;
        replay = 1;
        return 1;
    }
    else
        return 0;
}

int  progress_next_avail(void)
{
    if (next)
    {
        if (mode == MODE_CHALLENGE)
            return status == GAME_GOAL;
        else
            return level_opened(next);
    }
    return 0;
}

int  progress_same_avail(void)
{
    switch (status)
    {
    case GAME_NONE:
        return mode != MODE_CHALLENGE;

    default:
        if (mode == MODE_CHALLENGE)
            return !progress_dead();
        else
            return 1;
    }
}

int  progress_next(void)
{
    progress_stop();
    return progress_play(next);
}

int  progress_same(void)
{
    progress_stop();

    /* Reset progress and goal enabled state. */

    if (status == GAME_GOAL)
        curr = prev;

    same_goal_e = 1;

    return progress_play(level);
}

int  progress_dead(void)
{
    return mode == MODE_CHALLENGE ? curr.balls < 0 : 0;
}

int  progress_done(void)
{
    return done;
}

int  progress_last(void)
{
    return mode != MODE_CHALLENGE && status == GAME_GOAL && !next;
}

int  progress_lvl_high(void)
{
    return (time_rank < RANK_LAST ||
            goal_rank < RANK_LAST ||
            coin_rank < RANK_LAST);
}

int  progress_set_high(void)
{
    return (score_rank < RANK_LAST ||
            times_rank < RANK_LAST);
}

void progress_rename(int set_only)
{
    const char *player = config_get_s(CONFIG_PLAYER);

    if (set_only)
    {
        set_rename_player(score_rank, times_rank, player);
    }
    else
    {
        level_rename_player(level, time_rank, goal_rank, coin_rank, player);

        demo_rename_player(USER_REPLAY_FILE, player);

        if (progress_done())
            set_rename_player(score_rank, times_rank, player);
    }

    set_store_hs();
}

int  progress_reward_ball(int s)
{
    return s > 0 && s % 100 == 0;
}

/*---------------------------------------------------------------------------*/

struct level *curr_level(void) { return level; }

int curr_balls(void) { return curr.balls; }
int curr_score(void) { return curr.score; }
int curr_mode (void) { return mode;       }
int curr_bonus(void) { return bonus;      }
int curr_goal (void) { return goal;       }

int progress_time_rank(void) { return time_rank; }
int progress_goal_rank(void) { return goal_rank; }
int progress_coin_rank(void) { return coin_rank; }

int progress_times_rank(void) { return times_rank; }
int progress_score_rank(void) { return score_rank; }

/*---------------------------------------------------------------------------*/

const char *mode_to_str(int m, int l)
{
    switch (m)
    {
    case MODE_CHALLENGE: return l ? _("Challenge Mode") : _("Challenge");
    case MODE_NORMAL:    return l ? _("Normal Mode")    : _("Normal");
    default:             return l ? _("Unknown Mode")   : _("Unknown");
    }
}

/*---------------------------------------------------------------------------*/
