
/*
 *  Copyright(C) honwel http://www.honwel.net
 *
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#define SHM_SIZE 1024 * 1024 * 2


typedef struct {
    /* the total number of requests */
    ngx_atomic_t    stat_requests;
    /* response's length by nginx sended, contains header and body */
    ngx_atomic_t    stat_sent;
    /* upstream's response recv(bytes) which is response body's length */
    ngx_atomic_t    stat_response_recv;
    /* n requests per msec */
    ngx_atomic_t    stat_per_requests;
    /* average response time(msec) per requests */
    ngx_atomic_t    stat_avg_response_time;

    ngx_atomic_t    stat_requests_1m;
    ngx_atomic_t    stat_response_time_1m;
    ngx_atomic_t    stat_time;

    ngx_atomic_t    stat_request_20x;
    ngx_atomic_t    stat_request_30x;
    ngx_atomic_t    stat_request_40x;
    ngx_atomic_t    stat_request_50x;
} ngx_http_stubs_status_ctx_t;

typedef struct {
    ngx_http_stubs_status_ctx_t *ctx;
    ngx_shm_zone_t  *shm_zone;
    ssize_t          shm_size;
    ngx_slab_pool_t *shpool;

    ngx_int_t   startup; 
} ngx_http_stubs_status_conf_t;

static ngx_int_t ngx_http_stubs_status_handler_init(ngx_conf_t *cf);

static char *ngx_http_set_status(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static void * ngx_http_stubs_status_create_conf(ngx_conf_t *cf);

static char * ngx_http_stubs_status_init_conf(ngx_conf_t *cf, void *conf);

static void * ngx_http_stubs_status_create_srv_conf(ngx_conf_t *cf);


static ngx_command_t  ngx_http_status_commands[] = {

    { ngx_string("stubs_status"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_http_set_status,
      0,
      0,
      NULL },

      ngx_null_command
};

static ngx_http_module_t  ngx_http_stubs_status_module_ctx = {
    NULL,                                       /* preconfiguration */
    ngx_http_stubs_status_handler_init,         /* postconfiguration */

    ngx_http_stubs_status_create_conf,          /* create main configuration */
    ngx_http_stubs_status_init_conf,            /* init main configuration */

    ngx_http_stubs_status_create_srv_conf,      /* create server configuration */
    NULL,                                       /* merge server configuration */

    NULL,                                       /* create location configuration */
    NULL                                        /* merge location configuration */
};

ngx_module_t  ngx_http_stubs_status_module = {
    NGX_MODULE_V1,
    &ngx_http_stubs_status_module_ctx,     /* module context */
    ngx_http_status_commands,              /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_int_t
ngx_http_stubs_status_handler(ngx_http_request_t *r)
{
    size_t      size;
    ngx_int_t   rc;
    ngx_buf_t  *b;
    ngx_chain_t out;

    ngx_time_t *time;    

    ngx_http_stubs_status_conf_t *sscf;
    ngx_http_stubs_status_ctx_t  *ctx;

    ngx_atomic_int_t rq, st, rv, pr, at;
    ngx_atomic_int_t r2, r3, r4, r5;

    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    if (r->uri.data[r->uri.len - 1] == '/') {
        return NGX_DECLINED;
    }

    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK) {
        return rc;
    }

    /* set response content type */
    ngx_str_set(&r->headers_out.content_type, "text/plain");    
    
    if (r->method == NGX_HTTP_HEAD) {
        r->headers_out.status = NGX_HTTP_OK;

        rc = ngx_http_send_header(r);

        if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
            return rc;
        }
    }

    sscf = ngx_http_get_module_main_conf(r, ngx_http_stubs_status_module);
    if (NULL == sscf) {
        return NGX_ERROR;
    }

    size = sizeof("Uptime: \n") + NGX_ATOMIC_T_LEN
         + sizeof("upstream requests: \n")
         + sizeof("upstream sent: \n")
         + sizeof("upstream recv: \n")
         + sizeof("upstream reqs/per: \n")
         + sizeof("upstream resp_time/avg(ms): \n")
         + sizeof("-------------------------------------\n")
         + 8 + 5 * NGX_ATOMIC_T_LEN
         + sizeof("reqs_20x: \n") 
         + sizeof("reqs_30x: \n")
         + sizeof("reqs_40x: \n")
         + sizeof("reqs_50x: \n")
         + 4 * NGX_ATOMIC_T_LEN;

    /* create buf */
    b = ngx_create_temp_buf(r->pool, size);
    if (NULL == b) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    out.buf = b;
    out.next = NULL;

    /* fill stat data into buf */
    ctx = sscf->ctx;
    rq = ctx->stat_requests;
    st = ctx->stat_sent;
    rv = ctx->stat_response_recv;
    pr = ctx->stat_per_requests;
    at = ctx->stat_avg_response_time;
    r2 = ctx->stat_request_20x;
    r3 = ctx->stat_request_30x;
    r4 = ctx->stat_request_40x;
    r5 = ctx->stat_request_50x; 

    time = ngx_timeofday();     
    b->last = ngx_sprintf(b->last, "Uptime: %uA\n", time->sec - sscf->startup);
    
    b->last = ngx_sprintf(b->last, "upstream requests: %uA\n", rq);    
    b->last = ngx_sprintf(b->last, "upstream sent: %uA\n", st);    
    b->last = ngx_sprintf(b->last, "upstream recv: %uA\n", rv);    
    b->last = ngx_sprintf(b->last, "upstream reqs/per: %uA\n", pr);    
    b->last = ngx_sprintf(b->last, "upstream resp_time/avg(ms): %uA\n", at);    

    b->last = ngx_cpymem(b->last, "-------------------------------------\n",
        sizeof("-------------------------------------\n") - 1);

    b->last = ngx_sprintf(b->last, "reqs_20x: %uA\n", r2);     
    b->last = ngx_sprintf(b->last, "reqs_30x: %uA\n", r3);     
    b->last = ngx_sprintf(b->last, "reqs_40x: %uA\n", r4);     
    b->last = ngx_sprintf(b->last, "reqs_50x: %uA\n", r5);    


    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = b->last - b->pos;

    b->last_buf = (r == r->main) ? 1 : 0;

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_output_filter(r, &out);
}

static char *
ngx_http_set_status(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_stubs_status_handler;

    return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_stubs_status_request_handler(ngx_http_request_t *r)
{
    ngx_http_stubs_status_conf_t        *sscf;
    ngx_http_stubs_status_ctx_t         *ctx;

    ngx_http_upstream_state_t           *state;
    size_t          response_length = 0;
    ngx_atomic_t    response_time = 0;

    ngx_uint_t      i;
    ngx_msec_int_t  ms;

    sscf = ngx_http_get_module_main_conf(r, ngx_http_stubs_status_module);
    ctx = sscf->ctx;


    if (NULL == sscf || NULL == ctx) {
        return NGX_ERROR;
    }

    if (r->upstream_states != NULL)
    {
        i = 0;
        ms = 0;
        state = r->upstream_states->elts;

        for ( ;; ) {           
            if (state[i].status) {
                ms = (ngx_msec_int_t)
                        (state[i].response_sec * 1000 + state[i].response_msec);
                ms = ngx_max(ms, 0);
            }

            response_length += state[i].response_length;
            response_time += (ngx_atomic_t) ms;
            
            if (++i == r->upstream_states->nelts) {
                break;
            }
            
            //ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
            //    "state_response_time[%d]: \"%d\"", i, state[i].response_msec);
        }

        ngx_atomic_fetch_add(&ctx->stat_response_recv, response_length);
        ngx_atomic_fetch_add(&ctx->stat_response_time_1m, response_time);

        ngx_atomic_fetch_add(&ctx->stat_requests, 1);
        ngx_atomic_fetch_add(&ctx->stat_sent, r->connection->sent);    
    
        /* per and avg */    
        ngx_atomic_fetch_add(&ctx->stat_requests_1m, 1);

        ngx_time_t  *time = ngx_timeofday();

        ngx_shmtx_lock(&sscf->shpool->mutex);
        if (time->sec - ctx->stat_time >= 60) {
            if (0 == ctx->stat_requests_1m) {
                ctx->stat_per_requests = 0;
                ctx->stat_avg_response_time = 0;
            } else if (0 == ctx->stat_response_time_1m) {
                ctx->stat_avg_response_time = 0;
            } else {
                ctx->stat_per_requests = ctx->stat_requests_1m / 60;
                ctx->stat_avg_response_time = ctx->stat_response_time_1m 
                    / ctx->stat_requests_1m;
                //ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                //            "avg_response_time: \"%d\"", ctx->stat_avg_response_time);
            }
            time = ngx_timeofday();

            ctx->stat_time = time->sec;
            ctx->stat_requests_1m = 0;
            ctx->stat_response_time_1m = 0;
        }
        ngx_shmtx_unlock(&sscf->shpool->mutex);

        /* stat HTTP status */
        if (r->err_status) {
            if (r->err_status >= 400 && r->err_status < 410)
            {
                ngx_atomic_fetch_add(&ctx->stat_request_40x, 1);
            }else if (r->err_status >= 500 && r->err_status < 510)
            {
                ngx_atomic_fetch_add(&ctx->stat_request_50x, 1);
            }
        } else if (r->headers_out.status) {
            if (r->headers_out.status >= 200 && r->headers_out.status < 210)
            {
                ngx_atomic_fetch_add(&ctx->stat_request_20x, 1);
            }else if (r->headers_out.status >= 300 && r->headers_out.status < 310)
            {
                ngx_atomic_fetch_add(&ctx->stat_request_30x, 1);
            }
        }        
    } // if r->upstream_states != NULL
       
    return NGX_OK; 
}

static ngx_int_t
ngx_http_stubs_status_handler_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt         *h;
    ngx_http_core_main_conf_t   *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    /* set callback, after log phase */
    *h = ngx_http_stubs_status_request_handler;

    return NGX_OK;
}

static ngx_int_t  
ngx_http_stubs_status_init_zone(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_http_stubs_status_conf_t    *sscf;

    if (NULL == shm_zone) {
        return NGX_ERROR;
    }

    sscf = shm_zone->data;    
    sscf->shpool = (ngx_slab_pool_t *) shm_zone->shm.addr; 
    sscf->ctx = ngx_slab_alloc(sscf->shpool, 
        sizeof(ngx_http_stubs_status_ctx_t)); 
    
    return NGX_OK;
}

static void *
ngx_http_stubs_status_create_srv_conf(ngx_conf_t *cf)
{
    ngx_http_stubs_status_conf_t    *conf;
    
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_stubs_status_conf_t));
    if (NULL == conf) {
        return NULL;
    }

    return conf;
}

static void *
ngx_http_stubs_status_create_conf(ngx_conf_t *cf)
{
    ngx_http_stubs_status_conf_t    *conf;
    ngx_time_t  *time;
    
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_stubs_status_conf_t));
    if (NULL == conf) {
        return NULL;
    }
    /* share memory size*/
    conf->shm_size = SHM_SIZE;  
    /* init uptime */
    time = ngx_timeofday();
    conf->startup = time->sec;
    
    return conf;
}

static char *
ngx_http_stubs_status_init_conf(ngx_conf_t *cf, void *conf)
{
    ngx_http_stubs_status_conf_t    *sscf;
    ngx_str_t       name = ngx_string("http_stubs_status_zone");
    
    if (NULL == conf) {
        return NGX_CONF_ERROR;
    }
    sscf = conf;
    
    sscf->shm_zone = ngx_shared_memory_add(cf, &name, sscf->shm_size, 
        &ngx_http_stubs_status_module);
    sscf->shm_zone->init = ngx_http_stubs_status_init_zone;
    sscf->shm_zone->data = sscf;
                  
    return NGX_CONF_OK;
}

