#include "analysis.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { size_t n; double sum, sumsq; } Stats;
static void add(Stats *s, double v) { s->n++; s->sum += v; s->sumsq += v * v; }
static double mean(const Stats *s) { return s->n ? s->sum / s->n : 0.0; }
static double sd(const Stats *s) { double m=mean(s); return s->n>1 ? sqrt(fmax(0.0,(s->sumsq-s->n*m*m)/(s->n-1))) : 0.0; }
static double ci95(const Stats *s) { return s->n>1 ? 1.96*sd(s)/sqrt((double)s->n) : 0.0; }
static int attack_index(const char *s) { if(strstr(s,"51%"))return 0; if(strstr(s,"Selfish"))return 1; if(strstr(s,"withholding"))return 2; return 3; }
static int state_index(const char *s) { return strcmp(s,"NORMAL")==0?0:strcmp(s,"ATTAQUE")==0?1:2; }
static const char *attack_name(size_t index) {
    static const char *names[] = {"51% double spending", "Selfish mining", "Block withholding", "Sybil P2P"};
    return index < 4 ? names[index] : "Inconnue";
}
static bool has_csv_header(const char *directory, const char *file_name) {
    char file_path[640], header[128];
    FILE *file;
    if (snprintf(file_path, sizeof(file_path), "%s\\%s", directory, file_name) >= (int)sizeof(file_path)) return false;
    file = fopen(file_path, "r");
    if (!file) return false;
    if (!fgets(header, sizeof(header), file)) { fclose(file); return false; }
    fclose(file);
    return strchr(header, ',') != NULL;
}
bool analysis_run(const char *dir, AnalysisReport *report) {
    char path[640], line[2048], *fields[20]; FILE *in,*stats,*comparison,*notes; Stats loss[4][3]={0}, orphan[4][3]={0}, delay[4][3]={0}, work[4][3]={0}, cpu[4][3]={0}; size_t i,j;
    if(!dir||!report||!has_csv_header(dir,"detection.csv")||!has_csv_header(dir,"thresholds.csv")||!has_csv_header(dir,"performance.csv")||!has_csv_header(dir,"aggregate.csv")||snprintf(path,sizeof(path),"%s\\runs.csv",dir)>=(int)sizeof(path))return false;
    in=fopen(path,"r"); if(!in)return false; memset(report,0,sizeof(*report)); snprintf(report->campaign_path,sizeof(report->campaign_path),"%s",dir);
    if(!fgets(line,sizeof(line),in)){fclose(in);return false;}
    while(fgets(line,sizeof(line),in)){size_t n=0;char *p=line;while(n<20&&(fields[n]=strtok(n?NULL:p,",\r\n"))!=NULL){n++;p=NULL;}if(n<17)continue;{int a=attack_index(fields[3]),s=state_index(fields[5]);double o=strtod(fields[8],NULL),d=strtod(fields[9],NULL),l=strtod(fields[13],NULL),w=strtod(fields[14],NULL),c=strtod(fields[15],NULL);add(&loss[a][s],l);add(&orphan[a][s],o);add(&delay[a][s],d);add(&work[a][s],w);add(&cpu[a][s],c);report->samples++;}}
    fclose(in);
    if(snprintf(path,sizeof(path),"%s\\statistics.csv",dir)>=(int)sizeof(path))return false;
    stats=fopen(path,"w");if(!stats)return false;
    fprintf(stats,"attack,state,n,loss_mean,loss_sd,loss_ci95,orphans_mean,delay_mean,work_mean,cpu_mean\n");
    for(i=0;i<4;i++)for(j=0;j<3;j++)fprintf(stats,"%s,%s,%zu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.9f\n",attack_name(i),j==0?"NORMAL":j==1?"ATTAQUE":"PROTEGE",loss[i][j].n,mean(&loss[i][j]),sd(&loss[i][j]),ci95(&loss[i][j]),mean(&orphan[i][j]),mean(&delay[i][j]),mean(&work[i][j]),mean(&cpu[i][j]));
    fclose(stats);
    if(snprintf(path,sizeof(path),"%s\\comparison.csv",dir)>=(int)sizeof(path))return false;
    comparison=fopen(path,"w");if(!comparison)return false;
    fprintf(comparison,"attack,attack_loss_mean,protected_loss_mean,loss_reduction,attack_delay_mean,protected_delay_mean\n");
    for(i=0;i<4;i++)fprintf(comparison,"%s,%.6f,%.6f,%.6f,%.6f,%.6f\n",attack_name(i),mean(&loss[i][1]),mean(&loss[i][2]),mean(&loss[i][1])-mean(&loss[i][2]),mean(&delay[i][1]),mean(&delay[i][2]));
    fclose(comparison);
    if(snprintf(path,sizeof(path),"%s\\interpretation.md",dir)>=(int)sizeof(path))return false;
    notes=fopen(path,"w");if(!notes)return false;
    fputs("# Interpretation des resultats\n\n",notes); fprintf(notes,"L'analyse porte sur %zu lignes d'etat. Les moyennes et ecarts-types sont calcules par attaque et par etat. Les intervalles de confiance a 95 %% sont affiches seulement pour n > 1.\n\n",report->samples);
    fputs("Les pertes et delais sont des metriques simulees ; le temps CPU depend de la machine. Les resultats doivent donc etre interpretes comme une comparaison experimentale reproductible du simulateur, et non comme une mesure d'un reseau blockchain reel.\n",notes); fclose(notes); return report->samples>0;
}
void analysis_print_report(const AnalysisReport *report){printf("Analyse terminee : %zu lignes exploitees dans %s\n",report->samples,report->campaign_path);}
