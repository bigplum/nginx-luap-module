
/*
 * Copyright (C) Simon Lee @ Huawei
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

typedef struct {
    char                      *name;
} ngx_luap_process_ctx_t;

typedef struct {
    ngx_str_t  lua_file;
} ngx_luap_conf_t;

#define ngx_luap_get_conf(conf_ctx, module)                                  \
             (*(ngx_get_conf(conf_ctx, ngx_luap_module))) [module.ctx_index];


static ngx_luap_process_ctx_t  ngx_luap_process_ctx = {
    "lua attach process"
};

ngx_int_t ngx_luap_num = 0;

static void * ngx_luap_core_create_conf(ngx_cycle_t *cycle);
static char * ngx_luap_core_init_conf(ngx_cycle_t *cycle, void *conf);
static char *
ngx_luap_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void ngx_luap_process_cycle(ngx_cycle_t *cycle, void *data);
static ngx_int_t ngx_luap_init_process(ngx_cycle_t *cycle);
static int luaF_ngx_signal (lua_State *L);
static int luaF_ngx_status (lua_State *L);


static ngx_uint_t     ngx_luap_max_module;


static ngx_command_t  ngx_luap_commands[] = {

    { ngx_string("luap"),
      NGX_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_luap_block,
      0,
      0,
      NULL },

      ngx_null_command
};



static ngx_core_module_t  ngx_luap_module_ctx = {
    ngx_string("luap"),
    NULL,                                   /* create configuration */
    NULL                                    /* init configuration */
};


ngx_module_t  ngx_luap_module = {
    NGX_MODULE_V1,
    &ngx_luap_module_ctx,              /* module context */
    ngx_luap_commands,                 /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    ngx_luap_init_process,                 /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

#define LUA_NGX_CYCLE "_ngx.cycle" /* nginx cycle pointer */


#define NGX_LUAP_MODULE      0x5041554C  /* "LUAP" */
#define NGX_LUAP_CONF        0x02000000

typedef struct {
    void *(*create_conf)(ngx_cycle_t *cycle);
    char *(*init_conf)(ngx_cycle_t *cycle, void *conf);
} ngx_luap_module_t;


static ngx_command_t  ngx_luap_core_commands[] = {

    { ngx_string("lua_file"),
      NGX_LUAP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_luap_conf_t, lua_file),
      NULL },

      ngx_null_command
};

static ngx_luap_module_t  ngx_luap_core_module_ctx = {
    ngx_luap_core_create_conf,                  /* create configuration */
    ngx_luap_core_init_conf                     /* init configuration */
};

ngx_module_t  ngx_luap_core_module = {
    NGX_MODULE_V1,
    &ngx_luap_core_module_ctx,              /* module context */
    ngx_luap_core_commands,                 /* module directives */
    NGX_LUAP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_luap_core_create_conf(ngx_cycle_t *cycle)
{
    ngx_luap_conf_t  *apcf;

    apcf = ngx_palloc(cycle->pool, sizeof(ngx_luap_conf_t));
    if (apcf == NULL) {
        return NULL;
    }

    return apcf;
}

static char *
ngx_luap_core_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_luap_conf_t  *apcf = conf;

    return NGX_CONF_OK;
}


static char *
ngx_luap_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                 *rv;
    void               ***ctx;
    ngx_uint_t            i;
    ngx_conf_t            pcf;
    ngx_luap_module_t    *m;

    /* count the number of the event modules and set up their indices */

    ngx_luap_max_module = 0;
    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_LUAP_MODULE) {
            continue;
        }

        ngx_modules[i]->ctx_index = ngx_luap_max_module++;
    }

    ctx = ngx_pcalloc(cf->pool, sizeof(void *));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    *ctx = ngx_pcalloc(cf->pool, ngx_luap_max_module * sizeof(void *));
    if (*ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    *(void **) conf = ctx;

    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_LUAP_MODULE) {
            continue;
        }

        m = ngx_modules[i]->ctx;

        if (m->create_conf) {
            (*ctx)[ngx_modules[i]->ctx_index] = m->create_conf(cf->cycle);
            if ((*ctx)[ngx_modules[i]->ctx_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }

    pcf = *cf;
    cf->ctx = ctx;
    cf->module_type = NGX_LUAP_MODULE;
    cf->cmd_type = NGX_LUAP_CONF;

    rv = ngx_conf_parse(cf, NULL);

    *cf = pcf;

    if (rv != NGX_CONF_OK)
        return rv;

    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_LUAP_MODULE) {
            continue;
        }

        m = ngx_modules[i]->ctx;

        if (m->init_conf) {
            rv = m->init_conf(cf->cycle, (*ctx)[ngx_modules[i]->ctx_index]);
            if (rv != NGX_CONF_OK) {
                return rv;
            }
        }
    }

    return NGX_CONF_OK;
}

static void
ngx_luap_process_cycle(ngx_cycle_t *cycle, void *data)
{
    ngx_uint_t              i;
    ngx_luap_conf_t        *apcf;
    ngx_luap_process_ctx_t *ctx = data;
    ngx_setproctitle(ctx->name);

    apcf = ngx_luap_get_conf(cycle->conf_ctx, ngx_luap_core_module);

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, cycle->log, 0, 
                    "about to run %V", &apcf->lua_file);
    
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    
    lua_pushlightuserdata(L, cycle);
    lua_setglobal(L, LUA_NGX_CYCLE);

    lua_newtable(L); /* ngx */

    lua_pushcfunction(L, luaF_ngx_signal);
    lua_setfield(L, -2, "signal");

    lua_pushcfunction(L, luaF_ngx_status);
    lua_setfield(L, -2, "status");

    lua_setglobal(L, "ngx");

    if (luaL_loadfile(L, apcf->lua_file.data) || lua_pcall(L, 0, 0, 0)) {

        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "open or run lua file failed: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    
    exit(0);
}

static ngx_int_t
ngx_luap_init_process(ngx_cycle_t *cycle)
{
    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "luap init");

    ngx_spawn_process(cycle, ngx_luap_process_cycle, &ngx_luap_process_ctx,
              "lua attach process", NGX_PROCESS_NORESPAWN);
                  
    return NGX_OK;
}

static int luaF_ngx_signal (lua_State *L)
{
    char                         sig[64];
    char                        *p;
    size_t                       len;
    ngx_cycle_t                 *cycle;
    
    lua_getglobal(L, LUA_NGX_CYCLE);
    cycle = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (cycle == NULL) {
        return luaL_error(L, "no cycle object found");
    }

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "luap ngx.signal");

    p = lua_tolstring(L, 1, &len);
    if (len > 64) {
        return luaL_error(L, "invalid signal");
    }
    p = ngx_copy(sig, (u_char *)p, len);
    sig[len] = 0;

    if (ngx_strcmp(sig, "stop") == 0
        || ngx_strcmp(sig, "quit") == 0
        || ngx_strcmp(sig, "reopen") == 0
        || ngx_strcmp(sig, "reload") == 0)
    {
        ngx_log_debug2(NGX_LOG_DEBUG_CORE, cycle->log, 0, "luap send %s %d", sig, len);
        if(ngx_signal_process(cycle, sig) == 1) {
            return luaL_error(L, "send signal failed");
        }
        return 0;
    }

    ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                  "invalid signal: %s", sig);
    return luaL_error(L, "invalid signal");

}

static int luaF_ngx_status (lua_State *L)
{
    ngx_cycle_t                 *cycle;
    ngx_atomic_int_t             ap, hn, ac, rq, rd, wr;
    
    lua_getglobal(L, LUA_NGX_CYCLE);
    cycle = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (cycle == NULL) {
        return luaL_error(L, "no cycle object found");
    }
    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "luap ngx.status");

    ap = *ngx_stat_accepted;
    hn = *ngx_stat_handled;
    ac = *ngx_stat_active;
    rq = *ngx_stat_requests;
    rd = *ngx_stat_reading;
    wr = *ngx_stat_writing;

    /* todo: test on 64bit platform */
    lua_pushnumber(L, ap);
    lua_pushnumber(L, hn);
    lua_pushnumber(L, ac);
    lua_pushnumber(L, rq);
    lua_pushnumber(L, rd);
    lua_pushnumber(L, wr);
    
    return 6;

}

