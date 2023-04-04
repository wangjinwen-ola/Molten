/**
 * Copyright 2017 chuan-yun silkcutKs <silkcutbeta@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "molten_chain.h"

void mo_build_chain_header(mo_chain_t *pct, mo_span_builder *psb, char *ip);

/* only destory val key */
#if PHP_VERSION_ID < 70000
static void mo_key_destory_func(void *pDest)
{
    mo_chain_key_t **data = (mo_chain_key_t **)pDest;
    (*data)->val ? efree((*data)->val) : NULL;
    efree(*data);
}
#else
static void mo_key_destory_func(zval *pDest)
{
    mo_chain_key_t *data = (mo_chain_key_t *)Z_PTR_P(pDest);
    data->val ? efree(data->val) : NULL;
    efree(data);
}
#endif

/* {{{ Obtain local internal ip */
void mo_obtain_local_ip(char *ip)
{
    struct ifaddrs *myaddrs, *ifa;
    struct sockaddr_in *ipv4;
    char buf[INET_ADDRSTRLEN];
    int status;
    
    memset(ip, 0x00, INET_ADDRSTRLEN);
    strncpy(ip, "127.0.0.1", INET_ADDRSTRLEN);

    status = getifaddrs(&myaddrs); 
    if (status != 0) {
        //todo log 
        return;
    }

    for (ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next) {
        if (NULL == ifa->ifa_addr) {
            continue;
        }

        if ((ifa->ifa_flags & IFF_UP) == 0) {
            continue;
        }

        /* only support ipv4*/
        if (AF_INET == ifa->ifa_addr->sa_family) {
            ipv4 = (struct sockaddr_in *)(ifa->ifa_addr);
            if (inet_ntop(AF_INET, &ipv4->sin_addr, buf, INET_ADDRSTRLEN) != NULL) {

                /* only support  internal network */
                if ((strncasecmp(buf, "10", 2) == 0) ||
                    (strncasecmp(buf, "192", 3) == 0)) {
                    strncpy(ip, buf, INET_ADDRSTRLEN);
                    break;
                }
            }
        }
    }
    freeifaddrs(myaddrs);
}
/* }}} */

/* build chain header */
void mo_build_chain_header(mo_chain_t *pct, mo_span_builder *psb, char *ip)
{
    /* loaded header */
    mo_chain_header_t *pch = &(pct->pch);
    
    zval *server_addr;
    if (find_server_var("SERVER_ADDR", sizeof("SERVER_ADDR"), (void **)&server_addr) == SUCCESS) {
        strncpy(pch->ip, Z_STRVAL_P(server_addr), INET_ADDRSTRLEN);
    } else {
        strncpy(pch->ip, ip, INET_ADDRSTRLEN);
    }

    zval *server_port;
    if (find_server_var("SERVER_PORT", sizeof("SERVER_PORT"), (void **)&server_port) == SUCCESS) {
        pch->port = atoi(Z_STRVAL_P(server_port));
    }

    /* retrive key from header */
    if (pct->is_cli != 1) {
        HashTable *ht = pch->chain_header_key;
        zval *tmp = NULL;
        mo_chain_key_t *pck;
        for(zend_hash_internal_pointer_reset(ht); 
                zend_hash_has_more_elements(ht) == SUCCESS;
                zend_hash_move_forward(ht)) {
            
            if (mo_zend_hash_get_current_data(ht, (void **)&pck) == SUCCESS) {
                if (find_server_var(pck->receive_key, pck->receive_key_len, (void **)&tmp) == SUCCESS) {
                    if (Z_TYPE_P(tmp) == IS_STRING) {
                        pck->val = estrdup(Z_STRVAL_P(tmp));
                    }
                }
            }
        }
    }
    
    /* generate trace_id */
    if (!pch->trace_id->val) {
        rand64hex(&pch->trace_id->val);
    }

    /* push parent context to stack */
    if (pch->parent_span_id->val) {
        push_span_context_with_id(pct->span_stack, pct->pch.parent_span_id->val);
    }

    /* generate span_id */
    if (!pch->span_id->val) {
        push_span_context(pct->span_stack);
    } else {
        push_span_context_with_id(pct->span_stack, pct->pch.span_id->val);
    }

    /* sampled, after we will do it dynamics */
    if (!pch->sampled->val) {
        pch->sampled->val = estrdup("1");
    }

    /* control flags not used now */
    if (!pch->flags->val) {
        pch->flags->val = estrdup("0");
    }
}

/* init chain header */
void mo_init_chain_header(mo_chain_header_t *pch)
{
    ALLOC_HASHTABLE(pch->chain_header_key);
    zend_hash_init(pch->chain_header_key, 8, NULL,  mo_key_destory_func, 0);
   
    /* chain header */
    /* trace id */
    mo_chain_key_t *trace_id = (mo_chain_key_t *)emalloc(sizeof(mo_chain_key_t));
    trace_id->name = "trace_id";
    trace_id->receive_key = MOLTEN_REC_TRACE_ID;
    trace_id->receive_key_len = sizeof(MOLTEN_REC_TRACE_ID);
    trace_id->pass_key = MOLTEN_HEADER_TRACE_ID;
    trace_id->pass_key_len = sizeof(MOLTEN_HEADER_TRACE_ID) - 1;
    trace_id->is_pass = 1;
    trace_id->val = NULL;
    pch->trace_id = trace_id;

    /* span id */
    mo_chain_key_t *span_id = (mo_chain_key_t *)emalloc(sizeof(mo_chain_key_t));
    span_id->name = "span_id";
    span_id->receive_key = MOLTEN_REC_SPAN_ID;
    span_id->receive_key_len = sizeof(MOLTEN_REC_SPAN_ID);
    span_id->pass_key = MOLTEN_HEADER_SPAN_ID;
    span_id->pass_key_len = sizeof(MOLTEN_HEADER_SPAN_ID) - 1;
    span_id->val = NULL;
    pch->span_id = span_id;

    /* parent_span_id */
    mo_chain_key_t *parent_span_id = (mo_chain_key_t *)emalloc(sizeof(mo_chain_key_t));
    parent_span_id->name = "parent_span_id";
    parent_span_id->receive_key = MOLTEN_REC_PARENT_SPAN_ID;
    parent_span_id->receive_key_len = sizeof(MOLTEN_REC_PARENT_SPAN_ID);
    parent_span_id->pass_key = MOLTEN_HEADER_PARENT_SPAN_ID;
    parent_span_id->pass_key_len = sizeof(MOLTEN_HEADER_PARENT_SPAN_ID) - 1;
    parent_span_id->val = NULL;
    pch->parent_span_id = parent_span_id;

    /* sampled */
    mo_chain_key_t *sampled = (mo_chain_key_t *)emalloc(sizeof(mo_chain_key_t));
    sampled->name = "sampled";
    sampled->receive_key = MOLTEN_REC_SAMPLED;
    sampled->receive_key_len = sizeof(MOLTEN_REC_SAMPLED);
    sampled->pass_key = MOLTEN_HEADER_SAMPLED;
    sampled->pass_key_len = sizeof(MOLTEN_HEADER_SAMPLED) - 1;
    sampled->val = NULL;
    pch->sampled = sampled;

    /* flags */
    mo_chain_key_t *flags = (mo_chain_key_t *)emalloc(sizeof(mo_chain_key_t));
    flags->name = "flags";
    flags->receive_key = MOLTEN_REC_FLAGS;
    flags->receive_key_len = sizeof(MOLTEN_REC_FLAGS);
    flags->pass_key = MOLTEN_HEADER_FLAGS;
    flags->pass_key_len = sizeof(MOLTEN_HEADER_FLAGS) - 1;
    flags->val = NULL;
    pch->flags = flags;

    /* add chain key to hash */
    ADD_HASH_MOLTEN_KEY(pch->chain_header_key, trace_id);
    ADD_HASH_MOLTEN_KEY(pch->chain_header_key, span_id);
    ADD_HASH_MOLTEN_KEY(pch->chain_header_key, parent_span_id);
    ADD_HASH_MOLTEN_KEY(pch->chain_header_key, sampled);
    ADD_HASH_MOLTEN_KEY(pch->chain_header_key, flags);
}

/* pt chain header dtor */
void mo_chain_header_dtor(mo_chain_header_t *pch)
{
    zend_hash_destroy(pch->chain_header_key);
    FREE_HASHTABLE(pch->chain_header_key);
}

/* pt chain ctor */
void mo_chain_ctor(mo_chain_t *pct, mo_chain_log_t *pcl, mo_span_builder *psb, mo_stack *span_stack, char *service_name, char *ip)
{
    pct->pcl = pcl;
   
    if (pct->pch.is_sampled == 1) {
        /* request method */
        pct->method = (char *) SG(request_info).request_method;

        pct->span_stack = span_stack;

        /* init error list */
        MO_ALLOC_INIT_ZVAL(pct->error_list);
        array_init(pct->error_list);

        /* execute time */
        //pct->execute_begin_time = (long) SG(global_request_time) * 1000000.00;
        pct->execute_begin_time = mo_time_usec();
        pct->execute_end_time = 0;
        
        /* script */
        if (SG(request_info).path_translated != NULL) {
            pct->script = estrdup(SG(request_info).path_translated);
        } else {
            pct->script = NULL;
        }

        pct->request_uri = SG(request_info).request_uri;
        pct->query_string = SG(request_info).query_string;

        /* cli */
        pct->argc = SG(request_info).argc;
        pct->argv = (const char **)SG(request_info).argv;

        /* build chain header */
        mo_init_chain_header(&(pct->pch));
        mo_build_chain_header(pct, psb, ip);
    }
}

/* pt chain dtor */
void mo_chain_dtor(mo_chain_t *pct, mo_span_builder *psb, mo_stack *span_stack)
{
    if (pct->pch.is_sampled == 1) {
        pct->execute_end_time = mo_time_usec();

        /* add main span */
        zval *span;
        char *span_id;
        char *parent_span_id;

        retrieve_span_id(span_stack, &span_id);
        retrieve_parent_span_id(span_stack, &parent_span_id);
        
        if (pct->method == NULL) {
            psb->start_span(&span, (char *)pct->sapi, pct->pch.trace_id->val, span_id, parent_span_id,  pct->execute_begin_time, pct->execute_end_time, pct, AN_SERVER);
        } else {
            // psb->start_span(&span, (char *)pct->method, pct->pch.trace_id->val, span_id, parent_span_id,  pct->execute_begin_time, pct->execute_end_time, pct, AN_SERVER);
             if (pct->request_uri != NULL) {
                psb->start_span(&span, (char *)pct->request_uri, pct->pch.trace_id->val, span_id, parent_span_id,  pct->execute_begin_time, pct->execute_end_time, pct, AN_SERVER);
             }else{
                psb->start_span(&span, (char *)pct->method, pct->pch.trace_id->val, span_id, parent_span_id,  pct->execute_begin_time, pct->execute_end_time, pct, AN_SERVER);
             }
            psb->span_add_ba_ex(span, "http.method",  (char *)pct->method, pct->execute_begin_time, pct, BA_NORMAL);
        }

        /* add request uri */
        if (pct->request_uri != NULL) {
            zval *http_host = NULL;
            find_server_var("HTTP_HOST", sizeof("HTTP_HOST"), (void **)&http_host);
            /* can not find HTTP_HOST use SERVER_NAME */
            if (http_host == NULL || strncmp(Z_STRVAL_P(http_host), "", 1) == 0) {
                find_server_var("SERVER_NAME", sizeof("SERVER_NAME"), (void **)&http_host);
            }

            zval *request_uri = NULL;
            find_server_var("REQUEST_URI", sizeof("REQUEST_URI"), (void **)&request_uri);
            if (http_host != NULL && request_uri != NULL && MO_Z_TYPE_P(http_host) == IS_STRING && MO_Z_TYPE_P(request_uri) == IS_STRING) {
                int url_len = Z_STRLEN_P(http_host) + Z_STRLEN_P(request_uri) + sizeof("http://") + 2;
                char *url = emalloc(url_len);
                memset(url, 0x00, url_len);
                snprintf(url, url_len, "http://%s%s", Z_STRVAL_P(http_host), Z_STRVAL_P(request_uri));
                psb->span_add_ba_ex(span, "http.url", url, pct->execute_begin_time, pct, BA_NORMAL);
                psb->span_add_ba_ex(span, "http.request_uri", pct->request_uri, pct->execute_begin_time, pct, BA_NORMAL);
                efree(url);
            }
        }

        /* add script path */
        if (pct->script != NULL) {
            psb->span_add_ba_ex(span, "path", pct->script, pct->execute_begin_time, pct, BA_PATH);
            efree(pct->script);
            pct->script = NULL;
        }

        /* iterator eror list */
        HashTable *ht = Z_ARRVAL_P(pct->error_list);
        zval *error_string;
#if PHP_VERSION_ID < 70000
        for(zend_hash_internal_pointer_reset(ht); 
                zend_hash_has_more_elements(ht) == SUCCESS;
                zend_hash_move_forward(ht)) {
              
            if (mo_zend_hash_get_current_data(ht, (void **)&error_string) == SUCCESS) {
                if (MO_Z_TYPE_P(error_string) == IS_STRING) {
                    psb->span_add_ba_ex(span, "error", Z_STRVAL_P(error_string), pct->execute_begin_time, pct, BA_ERROR);
                }
            }
        }
#else
       ZEND_HASH_FOREACH_VAL(ht, error_string) {
            if (MO_Z_TYPE_P(error_string) == IS_STRING) {
                psb->span_add_ba_ex(span, "error", Z_STRVAL_P(error_string), pct->execute_begin_time, pct, BA_ERROR);
            }
       } ZEND_HASH_FOREACH_END();
#endif
        
        if (pct->is_cli == 1 && pct->argc > 1) {
            int i = 1;
            char argv[1024];
            bzero(argv, 1024);
            for(;i < pct->argc; i++) {
                strcat(argv, pct->argv[i]);
                strcat(argv, ",");
            }
            argv[1023] = '\0';
            psb->span_add_ba_ex(span, "argv", argv, pct->execute_begin_time, pct, BA_NORMAL);
        }

        mo_chain_add_span(pct->pcl, span);


        /* free error list */
        mo_zval_ptr_dtor(&pct->error_list);
        MO_FREE_ALLOC_ZVAL(pct->error_list);

        /* header dtor */
        mo_chain_header_dtor(&(pct->pch));

        /* pop span content */
        pop_span_context(span_stack);

        /* pop parent span content */
        pop_span_context(span_stack);
    }
}
