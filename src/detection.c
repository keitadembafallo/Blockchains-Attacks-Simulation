#include "detection.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

const char *detection_severity_name(DetectionSeverity s) {
    static const char *names[]={"INFO","FAIBLE","MOYEN","ELEVE","CRITIQUE"};
    return names[s > SEVERITY_CRITICAL ? SEVERITY_CRITICAL : s];
}
static DetectionSeverity severity(unsigned score) {
    return score>=80?SEVERITY_CRITICAL:score>=60?SEVERITY_HIGH:score>=40?SEVERITY_MEDIUM:score>=20?SEVERITY_LOW:SEVERITY_INFO;
}
static void add(DetectionReport *r,const char *attack,const char *suspect,unsigned score,const char *reason) {
    DetectionAlert *a;
    if(r->alert_count==DETECTION_MAX_ALERTS)return;
    a=&r->alerts[r->alert_count++]; memset(a,0,sizeof(*a));
    snprintf(a->attack,sizeof(a->attack),"%s",attack); snprintf(a->suspect,sizeof(a->suspect),"%s",suspect? suspect:"GLOBAL");
    snprintf(a->reason,sizeof(a->reason),"%s",reason); a->score=score>100?100:score; a->severity=severity(a->score);
    if(a->score>r->global_score)r->global_score=a->score;
    if(a->severity>r->maximum_severity)r->maximum_severity=a->severity;
}
void detection_analyze(const MetricsExperiment *e, DetectionReport *r) {
    const MetricSummary *m=&e->malicious; const AttackScenario *s=&e->scenario;
    size_t i; unsigned score=0;
    memset(r,0,sizeof(*r));
    if(m->invalid_chains) add(r,"INCOHERENCE","GLOBAL",100,"La validation de chaine a echoue.");
    if(s->type==ATTACK_51 && m->double_spends && m->reorganization_depth && m->orphaned_blocks) {
        score=90; if(s->attacker_count)score=100;
        for(i=0;i<s->attacker_count;++i)add(r,"51% / DOUBLE_DEPENSE",s->attackers[i],score,"Double depense, reorganisation et blocs orphelins observes.");
    } else if(s->type==ATTACK_SELFISH_MINING && m->private_blocks &&
               m->orphan_rate>=e->normal.orphan_rate+0.05) {
        for(i=0;i<s->attacker_count;++i)add(r,"SELFISH_MINING",s->attackers[i],80,"Blocs prives et hausse du taux de blocs orphelins.");
    } else if(s->type==ATTACK_BLOCK_WITHHOLDING && m->withheld_blocks &&
               e->attack.pool_attacked_rewards*10 < e->attack.pool_reference_rewards*9) {
        for(i=0;i<s->attacker_count;++i)add(r,"BLOCK_WITHHOLDING",s->attackers[i],80,"Blocs retenus et baisse superieure a 10% des recompenses de pool.");
    } else if(s->type==ATTACK_SYBIL && m->sybil_neighbors>=s->victim_degree &&
               m->average_delay>=e->normal.average_delay+1.0) {
        add(r,"SYBIL",s->victim,90,"Voisins Sybil satures et propagation retardee.");
    }
    if(m->reorganization_depth && m->orphan_rate>=e->normal.orphan_rate+0.05)
        add(r,"REORGANISATION","GLOBAL",60,"Reorganisation et taux d'orphelins anormal.");
    if(m->average_delay>=e->normal.average_delay+1.0)
        add(r,"PROPAGATION","GLOBAL",30,"Delai de propagation superieur a la reference.");
    for(i=0;i<s->attacker_count && i<BC_MAX_NODES;++i)r->node_scores[i]=r->global_score;
}
void detection_analyze_pair(const MetricSummary *reference, const MetricSummary *observed,
                            DetectionReport *report) {
    if (reference == NULL || observed == NULL || report == NULL) return;
    memset(report, 0, sizeof(*report));
    if (observed->invalid_chains) add(report, "INCOHERENCE", "GLOBAL", 100, "La validation de chaine a echoue.");
    if (observed->reorganization_depth && observed->orphan_rate >= reference->orphan_rate + 0.05)
        add(report, "REORGANISATION", "GLOBAL", 60, "Reorganisation et taux d'orphelins anormal.");
    if (observed->average_delay >= reference->average_delay + 1.0)
        add(report, "PROPAGATION", "GLOBAL", 30, "Delai de propagation superieur a la reference.");
}
static void thresholds_from(double values[][3], size_t count, AdaptiveThresholds *t) {
    size_t i; double sum[3]={0}, sq[3]={0};
    memset(t,0,sizeof(*t)); t->count=count; t->window=DETECTION_BASELINE_WINDOW;
    for(i=0;i<count;++i){size_t j;for(j=0;j<3;++j){sum[j]+=values[i][j];sq[j]+=values[i][j]*values[i][j];}}
    if(count){t->orphan_mean=sum[0]/count;t->delay_mean=sum[1]/count;t->reorg_mean=sum[2]/count;
      t->orphan_std=sqrt(fmax(0,sq[0]/count-t->orphan_mean*t->orphan_mean));
      t->delay_std=sqrt(fmax(0,sq[1]/count-t->delay_mean*t->delay_mean));
      t->reorg_std=sqrt(fmax(0,sq[2]/count-t->reorg_mean*t->reorg_mean));}
    t->orphan_threshold=t->orphan_mean+2*t->orphan_std+0.05;
    t->delay_threshold=t->delay_mean+2*t->delay_std+1.0;
    t->reorg_threshold=t->reorg_mean+2*t->reorg_std+1.0;
    t->warmed_up=count>=5;
}
bool detection_load_baseline(const char *path, AdaptiveThresholds *t) {
    FILE *f; char line[256]; double v[DETECTION_BASELINE_WINDOW][3]; size_t n=0;
    if (!path || !t) return false;
    f = fopen(path, "r");
    if (!f) {
        memset(t, 0, sizeof(*t));
        t->window = DETECTION_BASELINE_WINDOW;
        return true;
    }
    while(fgets(line,sizeof(line),f)){double a,b,c;if(sscanf(line,"%lf,%lf,%lf",&a,&b,&c)==3){if(n<DETECTION_BASELINE_WINDOW){v[n][0]=a;v[n][1]=b;v[n][2]=c;n++;}}}
    fclose(f); thresholds_from(v,n,t); return true;
}
bool detection_update_baseline(const char *path,const MetricSummary *normal,size_t window) {
    FILE *f; double v[DETECTION_BASELINE_WINDOW][3]; size_t n=0,i; char line[256];
    if (!path || !normal) return false;
    if (window == 0 || window > DETECTION_BASELINE_WINDOW) window = DETECTION_BASELINE_WINDOW;
    f=fopen(path,"r"); if(f){while(fgets(line,sizeof(line),f)){double a,b,c;if(sscanf(line,"%lf,%lf,%lf",&a,&b,&c)==3&&n<window){v[n][0]=a;v[n][1]=b;v[n][2]=c;n++;}}fclose(f);}
    if(n==window){memmove(v,v+1,(window-1)*sizeof(v[0]));n--;} v[n][0]=normal->orphan_rate;v[n][1]=normal->average_delay;v[n][2]=normal->reorganization_depth;n++;
    f=fopen(path,"w");if(!f)return false; for(i=0;i<n;++i)fprintf(f,"%.9f,%.9f,%.9f\n",v[i][0],v[i][1],v[i][2]);return fclose(f)==0;
}
void detection_print_report(const DetectionReport *r) {
    size_t i; puts("\n========== DETECTION ==========");
    if(!r->alert_count){puts("Aucune anomalie significative detectee.");return;}
    for(i=0;i<r->alert_count;++i)printf("[%s | %u] %s | suspect: %s | %s\n",
      detection_severity_name(r->alerts[i].severity),r->alerts[i].score,r->alerts[i].attack,r->alerts[i].suspect,r->alerts[i].reason);
}
bool detection_export_csv(const DetectionReport *r,const MetricsExperiment *e) {
    char path[METRICS_PATH_SIZE+32]; FILE *f; size_t i;
    if(!e->export_path[0]||snprintf(path,sizeof(path),"%s\\detection.csv",e->export_path)>=(int)sizeof(path))return false;
    f=fopen(path,"w");if(!f)return false; fprintf(f,"attack,suspect,score,severity,reason\n");
    for(i=0;i<r->alert_count;++i)fprintf(f,"%s,%s,%u,%s,%s\n",r->alerts[i].attack,r->alerts[i].suspect,r->alerts[i].score,detection_severity_name(r->alerts[i].severity),r->alerts[i].reason);
    if(fclose(f)!=0)return false;
    if(snprintf(path,sizeof(path),"%s\\detection_nodes.csv",e->export_path)>=(int)sizeof(path))return false;
    f=fopen(path,"w");if(!f)return false; fprintf(f,"node_index,score\n");
    for(i=0;i<BC_MAX_NODES;++i)if(r->node_scores[i])fprintf(f,"%zu,%u\n",i,r->node_scores[i]);
    return fclose(f)==0;
}
