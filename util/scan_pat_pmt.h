#if !defined (__SCAN_PAT_PMT_H)
#define __SCAN_PAT_PMT_H

typedef struct
{
   int service_id;
   int ttx_pid;
} scan_services_t;

extern int scan_pat_pmt(const char * demux, scan_services_t * srv, int srv_cnt);

#endif  /* __SCAN_PAT_PMT_H */
