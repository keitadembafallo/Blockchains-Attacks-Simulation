#include "evaluation.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <errno.h>
#endif

typedef struct { double values[20][3]; size_t count; } LocalBaseline;
typedef struct { size_t tp, fp, tn, fn; double tick_sum; size_t detected; } Performance;
static uint64_t derive_seed(uint64_t seed, size_t index) {
    uint64_t value = seed ? seed : 1;
    size_t i;
    for (i = 0; i <= index; ++i) { value ^= value << 13; value ^= value >> 7; value ^= value << 17; }
    return value;
}
static unsigned config_fingerprint(const EvaluationConfig *config) {
    uint64_t h = config->seed_base ^ (config->seed_count * 0x9e3779b97f4a7c15ULL);
    size_t i;
    for (i = 0; i < config->node_count_count; ++i) h = (h * 33) ^ config->node_counts[i];
    for (i = 0; i < config->delay_count; ++i) h = (h * 33) ^ config->delays[i];
    for (i = 0; i < 4; ++i) h = (h * 33) ^ config->attacks[i];
    return (unsigned)(h ^ (h >> 32));
}

static double cpu_seconds(clock_t start) { return (double)(clock() - start) / CLOCKS_PER_SEC; }
static bool make_dir(const char *path) {
#ifdef _WIN32
    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return mkdir(path, 0777) == 0 || errno == EEXIST;
#endif
}
static uint64_t chain_work(const Blockchain *chain) {
    uint64_t value = 0; size_t i;
    for (i = 1; i < chain->block_count; ++i)
        if (chain->blocks[i].consensus == CONSENSUS_POW) value += chain->blocks[i].nonce + 1;
    return value;
}
static void baseline_thresholds(const LocalBaseline *base, AdaptiveThresholds *thresholds) {
    size_t i, j; double sum[3] = {0}, sq[3] = {0};
    memset(thresholds, 0, sizeof(*thresholds)); thresholds->count = base->count; thresholds->window = 20;
    for (i = 0; i < base->count; ++i) for (j = 0; j < 3; ++j) { sum[j] += base->values[i][j]; sq[j] += base->values[i][j] * base->values[i][j]; }
    if (base->count) {
        thresholds->orphan_mean = sum[0] / base->count; thresholds->delay_mean = sum[1] / base->count; thresholds->reorg_mean = sum[2] / base->count;
        thresholds->orphan_std = sqrt(fmax(0.0, sq[0] / base->count - thresholds->orphan_mean * thresholds->orphan_mean));
        thresholds->delay_std = sqrt(fmax(0.0, sq[1] / base->count - thresholds->delay_mean * thresholds->delay_mean));
        thresholds->reorg_std = sqrt(fmax(0.0, sq[2] / base->count - thresholds->reorg_mean * thresholds->reorg_mean));
    }
    thresholds->orphan_threshold = thresholds->orphan_mean + 2.0 * thresholds->orphan_std + 0.05;
    thresholds->delay_threshold = thresholds->delay_mean + 2.0 * thresholds->delay_std + 1.0;
    thresholds->reorg_threshold = thresholds->reorg_mean + 2.0 * thresholds->reorg_std + 1.0;
    thresholds->warmed_up = base->count >= 5;
}
static void baseline_add(LocalBaseline *base, const MetricSummary *normal) {
    if (base->count == 20) { memmove(base->values, base->values + 1, 19 * sizeof(base->values[0])); base->count--; }
    base->values[base->count][0] = normal->orphan_rate;
    base->values[base->count][1] = normal->average_delay;
    base->values[base->count][2] = (double)normal->reorganization_depth;
    base->count++;
}
static bool report_positive(const DetectionReport *report) { return report->maximum_severity >= SEVERITY_HIGH; }
static bool adaptive_positive(const MetricsExperiment *e, const AdaptiveThresholds *t, bool fixed) {
    const MetricSummary *m = &e->malicious;
    if (!t->warmed_up) return fixed;
    if (e->scenario.type == ATTACK_51) return m->double_spends && m->reorganization_depth > 0 && m->orphan_rate >= t->orphan_threshold;
    if (e->scenario.type == ATTACK_SELFISH_MINING) return m->private_blocks > 0 && m->orphan_rate >= t->orphan_threshold;
    if (e->scenario.type == ATTACK_BLOCK_WITHHOLDING) return m->withheld_blocks > 0 && e->attack.pool_attacked_rewards * 10 < e->attack.pool_reference_rewards * 9;
    return m->sybil_neighbors >= e->scenario.victim_degree && m->average_delay >= t->delay_threshold;
}
static bool build_network(Blockchain *chain, size_t nodes, uint64_t seed, AttackType type, Consensus consensus) {
    size_t i; uint64_t coalition = type == ATTACK_51 ? 55 + 5 * (seed % 3) : 35 + 5 * (seed % 3);
    uint64_t left = 100 - coalition;
    bc_init(chain, consensus, 0, seed, 10);
    for (i = 0; i < nodes; ++i) {
        char name[BC_NAME_SIZE]; uint64_t weight = i == 0 ? coalition : left / (nodes - 1) + (i - 1 < left % (nodes - 1) ? 1 : 0);
        snprintf(name, sizeof(name), "N%zu", i);
        if (!bc_add_node(chain, name, weight, weight, 100)) return false;
    }
    return true;
}
static void build_scenario(AttackScenario *s, AttackType type, size_t nodes, size_t delay, uint64_t seed) {
    memset(s, 0, sizeof(*s)); s->type = type; s->seed = seed; s->rounds = 20; s->propagation_delay = delay;
    s->consensus = (type == ATTACK_51 || type == ATTACK_SYBIL) && seed % 2 ? CONSENSUS_POS : CONSENSUS_POW;
    if (type == ATTACK_51) {
        s->amount = 10; s->confirmations = 1; s->attacker_count = 1;
        snprintf(s->attackers[0], BC_NAME_SIZE, "N0"); snprintf(s->spender, BC_NAME_SIZE, "N0");
        snprintf(s->merchant, BC_NAME_SIZE, "N1"); snprintf(s->private_recipient, BC_NAME_SIZE, "N0");
    } else if (type == ATTACK_SELFISH_MINING) {
        s->consensus = CONSENSUS_POW; s->attacker_count = 1; s->gamma = (unsigned)(50 * (seed % 3));
        snprintf(s->attackers[0], BC_NAME_SIZE, "N0");
    } else if (type == ATTACK_BLOCK_WITHHOLDING) {
        s->consensus = CONSENSUS_POW; s->attacker_count = 1; s->pool_member_count = nodes < 3 ? nodes : 3;
        snprintf(s->attackers[0], BC_NAME_SIZE, "N0");
        for (size_t i = 0; i < s->pool_member_count; ++i) snprintf(s->pool_members[i], BC_NAME_SIZE, "N%zu", i);
    } else {
        s->victim_degree = nodes < 4 ? nodes - 1 : 3; s->sybil_count = s->victim_degree;
        snprintf(s->victim, BC_NAME_SIZE, "N1");
    }
}
static double economic_loss(const MetricsExperiment *e, const MitigationResult *m, int state) {
    if (state == 0) return 0.0;
    if (e->scenario.type == ATTACK_51) return state == 2 && m->applied ? 0.0 : (double)e->attack.merchant_loss;
    if (e->scenario.type == ATTACK_SELFISH_MINING) return state == 2 && m->applied ? 0.0 : (double)e->attack.attacker_rewards;
    if (e->scenario.type == ATTACK_BLOCK_WITHHOLDING) {
        double loss = (double)(e->attack.pool_reference_rewards - e->attack.pool_attacked_rewards);
        return state == 2 ? loss - (double)m->prevented_attacker_reward : loss;
    }
    return state == 2 && m->applied ? 0.0 : (double)(e->attack.victim_reference_height - e->attack.victim_seen_height) * 10.0;
}
static void write_svg(const char *path, const char *title, const char *labels[], const double values[], size_t count) {
    FILE *f = fopen(path, "w"); size_t i; double max = 1.0;
    if (!f) return;
    for (i = 0; i < count; ++i) if (values[i] > max) max = values[i];
    fprintf(f, "<svg xmlns='http://www.w3.org/2000/svg' width='760' height='360'><style>text{font-family:Arial;font-size:14px}.bar{fill:#277da1}</style><text x='25' y='28' font-size='20'>%s</text>", title);
    for (i = 0; i < count; ++i) { double h = 220.0 * values[i] / max; double x = 45.0 + i * (650.0 / count);
        fprintf(f, "<rect class='bar' x='%.1f' y='%.1f' width='70' height='%.1f'/><text x='%.1f' y='300'>%s</text><text x='%.1f' y='%.1f'>%.2f</text>", x, 270.0-h, h, x, labels[i], x, 260.0-h, values[i]); }
    fputs("</svg>", f); fclose(f);
}
void evaluation_config_default(EvaluationConfig *config) {
    memset(config, 0, sizeof(*config)); config->seed_base = 100; config->seed_count = 3;
    config->node_counts[0]=3; config->node_counts[1]=5; config->node_counts[2]=8; config->node_count_count=3;
    config->delays[0]=1; config->delays[1]=2; config->delays[2]=3; config->delay_count=3;
    memset(config->attacks, 1, sizeof(config->attacks)); config->output_directory = "data\\evaluations";
}
void evaluation_config_quick(EvaluationConfig *config) {
    evaluation_config_default(config);
    config->quick_profile = true; config->seed_count = 1; config->node_count_count = 1; config->delay_count = 1;
    config->node_counts[0] = 3; config->delays[0] = 1;
}
static void add_performance(Performance *p, bool truth, bool positive, size_t tick) {
    if (truth) { if (positive) { p->tp++; p->tick_sum += tick; p->detected++; } else p->fn++; }
    else if (positive) p->fp++; else p->tn++;
}
bool evaluation_run_campaign(const EvaluationConfig *input, EvaluationReport *report) {
    EvaluationConfig config; LocalBaseline baseline = {0}; FILE *runs, *det, *thr, *campaign; size_t a, si, ni, di; bool first_attack;
    char path[EVALUATION_PATH_SIZE + 64], charts[EVALUATION_PATH_SIZE + 64]; Performance fixed[4] = {0}, adaptive[4] = {0};
    if (!input || !report || input->seed_count == 0 || input->seed_count > EVALUATION_MAX_SEEDS) return false;
    memset(report, 0, sizeof(*report)); config = *input; report->config = config;
    if (!make_dir(config.output_directory) || snprintf(report->output_path, sizeof(report->output_path), "%s\\campaign_%08x", config.output_directory, config_fingerprint(&config)) >= (int)sizeof(report->output_path) || !make_dir(report->output_path)) return false;
    if (strlen(report->output_path) + sizeof("\\charts") > sizeof(charts)) return false;
    strcpy(charts, report->output_path);
    strcat(charts, "\\charts");
    if(!make_dir(charts)) return false;
    snprintf(path,sizeof(path),"%s\\runs.csv",report->output_path); runs=fopen(path,"w"); if(!runs)return false;
    snprintf(path,sizeof(path),"%s\\detection.csv",report->output_path); det=fopen(path,"w"); if(!det){fclose(runs);return false;}
    snprintf(path,sizeof(path),"%s\\thresholds.csv",report->output_path); thr=fopen(path,"w"); if(!thr){fclose(runs);fclose(det);return false;}
    snprintf(path,sizeof(path),"%s\\campaign.csv",report->output_path); campaign=fopen(path,"w"); if(!campaign){fclose(runs);fclose(det);fclose(thr);return false;}
    fprintf(runs,"seed,nodes,delay,attack,consensus,state,success,blocks,orphans,avg_delay,reorg,rewards,attacker_rewards,economic_loss,logical_work,cpu_seconds,interventions\n");
    fprintf(det,"seed,nodes,delay,attack,method,positive,detection_tick,warmed_up\n");
    fprintf(thr,"seed,nodes,delay,baseline_count,warmed_up,orphan_mean,orphan_threshold,delay_mean,delay_threshold,reorg_mean,reorg_threshold\n");
    fprintf(campaign,"version,profile,seed_base,seeds,node_counts,delays,attacks\n1,%s,%llu,%zu,",config.quick_profile?"quick":"complete",(unsigned long long)config.seed_base,config.seed_count);
    for (ni = 0; ni < config.node_count_count; ++ni) fprintf(campaign, "%s%zu", ni ? "|" : "", config.node_counts[ni]);
    fputc(',', campaign);
    for (di = 0; di < config.delay_count; ++di) fprintf(campaign, "%s%zu", di ? "|" : "", config.delays[di]);
    fputc(',', campaign);
    first_attack = true;
    for (a = 0; a < 4; ++a) if (config.attacks[a]) { fprintf(campaign, "%s%s", first_attack ? "" : "|", a == 0 ? "51" : a == 1 ? "selfish" : a == 2 ? "withholding" : "sybil"); first_attack = false; }
    fputc('\n', campaign);
    for (si=0;si<config.seed_count;++si) for (ni=0;ni<config.node_count_count;++ni) for (di=0;di<config.delay_count;++di) for (a=0;a<4;++a) if (config.attacks[a]) {
        Blockchain chain; AttackScenario scenario; MetricsExperiment experiment; DetectionReport detection, normal_detection; MitigationConfig protection; MitigationResult mitigation; AdaptiveThresholds thresholds; EvaluationRun *row; clock_t start; double sim, detect_cost, protect_cost; uint64_t seed=derive_seed(config.seed_base,si);
        if(report->run_count==EVALUATION_MAX_RUNS){fclose(runs);fclose(det);fclose(thr);fclose(campaign);return false;}
        build_scenario(&scenario,(AttackType)a,config.node_counts[ni],config.delays[di],seed);
        if(!build_network(&chain,config.node_counts[ni],seed,(AttackType)a,scenario.consensus)){fclose(runs);fclose(det);fclose(thr);fclose(campaign);return false;}
        baseline_thresholds(&baseline,&thresholds); start=clock(); if(!metrics_run_experiment(&chain,&scenario,&experiment)){fclose(runs);fclose(det);fclose(thr);fclose(campaign);return false;} sim=cpu_seconds(start);
        start=clock(); detection_analyze(&experiment,&detection); detection_analyze_pair(&experiment.normal,&experiment.normal,&normal_detection); detect_cost=cpu_seconds(start); mitigation_config_default(&protection); start=clock(); if(!mitigation_apply(&experiment,&detection,&protection,&mitigation)){fclose(runs);fclose(det);fclose(thr);fclose(campaign);return false;} protect_cost=cpu_seconds(start);
        row=&report->runs[report->run_count++]; memset(row,0,sizeof(*row)); row->seed=seed;row->nodes=config.node_counts[ni];row->delay=config.delays[di];row->attack=(AttackType)a;row->consensus=scenario.consensus;row->attack_success=experiment.attack.success;row->mitigation_applied=mitigation.applied;row->attack_blocked=mitigation.adoption_blocked || (mitigation.applied && economic_loss(&experiment,&mitigation,2) < economic_loss(&experiment,&mitigation,1));row->fixed_positive=report_positive(&detection);row->adaptive_positive=adaptive_positive(&experiment,&thresholds,row->fixed_positive);row->fixed_tick=row->fixed_positive?experiment.attack.rounds_completed:0;row->adaptive_tick=row->adaptive_positive?experiment.attack.rounds_completed:0;row->normal_loss=economic_loss(&experiment,&mitigation,0);row->attack_loss=economic_loss(&experiment,&mitigation,1);row->protected_loss=economic_loss(&experiment,&mitigation,2);row->residual_impact=row->protected_loss;row->normal_work=chain_work(&experiment.attack.reference_chain);row->attack_work=chain_work(&experiment.attack.attacked_chain);row->protected_work=chain_work(&mitigation.protected_chain);row->simulation_cpu=sim;row->detection_cpu=detect_cost;row->mitigation_cpu=protect_cost;row->interventions=mitigation.intervention_count;
        fprintf(runs,"%llu,%zu,%zu,%s,%s,NORMAL,0,%zu,%zu,%.6f,%zu,%llu,%llu,%.6f,%llu,%.9f,0\n",(unsigned long long)seed,row->nodes,row->delay,attack_type_name(row->attack),bc_consensus_name(row->consensus),experiment.normal.blocks_produced,experiment.normal.orphaned_blocks,experiment.normal.average_delay,experiment.normal.reorganization_depth,(unsigned long long)experiment.normal.total_rewards,(unsigned long long)experiment.normal.attacker_rewards,row->normal_loss,(unsigned long long)row->normal_work,sim);
        fprintf(runs,"%llu,%zu,%zu,%s,%s,ATTAQUE,%d,%zu,%zu,%.6f,%zu,%llu,%llu,%.6f,%llu,%.9f,0\n",(unsigned long long)seed,row->nodes,row->delay,attack_type_name(row->attack),bc_consensus_name(row->consensus),row->attack_success?1:0,experiment.malicious.blocks_produced,experiment.malicious.orphaned_blocks,experiment.malicious.average_delay,experiment.malicious.reorganization_depth,(unsigned long long)experiment.malicious.total_rewards,(unsigned long long)experiment.malicious.attacker_rewards,row->attack_loss,(unsigned long long)row->attack_work,sim);
        fprintf(runs,"%llu,%zu,%zu,%s,%s,PROTEGE,%d,%zu,%zu,%.6f,%zu,%llu,%llu,%.6f,%llu,%.9f,%zu\n",(unsigned long long)seed,row->nodes,row->delay,attack_type_name(row->attack),bc_consensus_name(row->consensus),(row->attack_success && !row->attack_blocked)?1:0,experiment.protected.blocks_produced,experiment.protected.orphaned_blocks,experiment.protected.average_delay,experiment.protected.reorganization_depth,(unsigned long long)experiment.protected.total_rewards,(unsigned long long)experiment.protected.attacker_rewards,row->protected_loss,(unsigned long long)row->protected_work,protect_cost,row->interventions);
        fprintf(det,"%llu,%zu,%zu,%s,FIXE,%d,%zu,%d\n",(unsigned long long)seed,row->nodes,row->delay,attack_type_name(row->attack),row->fixed_positive?1:0,row->fixed_tick,thresholds.warmed_up?1:0);
        fprintf(det,"%llu,%zu,%zu,%s,ADAPTATIF,%d,%zu,%d\n",(unsigned long long)seed,row->nodes,row->delay,attack_type_name(row->attack),row->adaptive_positive?1:0,row->adaptive_tick,thresholds.warmed_up?1:0);
        fprintf(thr,"%llu,%zu,%zu,%zu,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",(unsigned long long)seed,row->nodes,row->delay,thresholds.count,thresholds.warmed_up?1:0,thresholds.orphan_mean,thresholds.orphan_threshold,thresholds.delay_mean,thresholds.delay_threshold,thresholds.reorg_mean,thresholds.reorg_threshold);
        add_performance(&fixed[a],true,row->fixed_positive,row->fixed_tick); add_performance(&adaptive[a],true,row->adaptive_positive,row->adaptive_tick); add_performance(&fixed[a],false,report_positive(&normal_detection),0); add_performance(&adaptive[a],false,false,0); baseline_add(&baseline,&experiment.normal);
    }
    fclose(runs);fclose(det);fclose(thr);fclose(campaign);
    snprintf(path,sizeof(path),"%s\\performance.csv",report->output_path); { FILE*f=fopen(path,"w"); if(!f)return false; fprintf(f,"attack,method,tp,fp,tn,fn,precision,recall,f1,false_positive_rate,avg_detection_tick\n"); for(a=0;a<4;++a)for(size_t m=0;m<2;++m){Performance*p=m?&adaptive[a]:&fixed[a];double precision=p->tp+p->fp?(double)p->tp/(p->tp+p->fp):0,recall=p->tp+p->fn?(double)p->tp/(p->tp+p->fn):0,f1=precision+recall?2*precision*recall/(precision+recall):0,fpr=p->fp+p->tn?(double)p->fp/(p->fp+p->tn):0;fprintf(f,"%s,%s,%zu,%zu,%zu,%zu,%.6f,%.6f,%.6f,%.6f,%.6f\n",attack_type_name((AttackType)a),m?"ADAPTATIF":"FIXE",p->tp,p->fp,p->tn,p->fn,precision,recall,f1,fpr,p->detected?p->tick_sum/p->detected:0);}fclose(f);}
    snprintf(path,sizeof(path),"%s\\aggregate.csv",report->output_path); { FILE*f=fopen(path,"w"); double loss[3]={0},work[3]={0},cpu[3]={0};size_t count=report->run_count; if(!f)return false;for(si=0;si<count;++si){loss[0]+=report->runs[si].normal_loss;loss[1]+=report->runs[si].attack_loss;loss[2]+=report->runs[si].protected_loss;work[0]+=report->runs[si].normal_work;work[1]+=report->runs[si].attack_work;work[2]+=report->runs[si].protected_work;cpu[1]+=report->runs[si].simulation_cpu;cpu[2]+=report->runs[si].mitigation_cpu;}fprintf(f,"state,avg_loss,avg_logical_work,avg_cpu_seconds\nNORMAL,%.6f,%.6f,0\nATTAQUE,%.6f,%.6f,%.9f\nPROTEGE,%.6f,%.6f,%.9f\n",loss[0]/count,work[0]/count,loss[1]/count,work[1]/count,cpu[1]/count,loss[2]/count,work[2]/count,cpu[2]/count);fclose(f);}
    { const char *labels[]={"NORMAL","ATTAQUE","PROTEGE"}; double losses[3]={0},works[3]={0},success[4]={0},f1s[2]={0}; for(si=0;si<report->run_count;++si){losses[0]+=report->runs[si].normal_loss;losses[1]+=report->runs[si].attack_loss;losses[2]+=report->runs[si].protected_loss;works[0]+=report->runs[si].normal_work;works[1]+=report->runs[si].attack_work;works[2]+=report->runs[si].protected_work;success[report->runs[si].attack]+=report->runs[si].attack_success?1:0;}for(a=0;a<4;++a){success[a]/=report->run_count/4;f1s[0]+=(fixed[a].tp+fixed[a].fn)?(double)fixed[a].tp/(fixed[a].tp+fixed[a].fn):0;f1s[1]+=(adaptive[a].tp+adaptive[a].fn)?(double)adaptive[a].tp/(adaptive[a].tp+adaptive[a].fn):0;}for(a=0;a<3;++a){losses[a]/=report->run_count;works[a]/=report->run_count;}f1s[0]/=4;f1s[1]/=4;strcpy(path,charts);strcat(path,"\\economic_loss.svg");write_svg(path,"Pertes economiques",labels,losses,3);strcpy(path,charts);strcat(path,"\\protection_cost.svg");write_svg(path,"Travail logique",labels,works,3);{const char*al[]={"51%","SELFISH","WITHHOLD","SYBIL"};strcpy(path,charts);strcat(path,"\\attack_success.svg");write_svg(path,"Taux de succes des attaques",al,success,4);} {const char*dl[]={"FIXE","ADAPTATIF"};strcpy(path,charts);strcat(path,"\\detection_recall.svg");write_svg(path,"Rappel de detection",dl,f1s,2);} }
    { const char *labels[]={"FIXE","ADAPTATIF"}; double delays[2]={0,0}; size_t counts[2]={0,0};
      for(si=0;si<report->run_count;++si) { if(report->runs[si].fixed_tick){delays[0]+=report->runs[si].fixed_tick;counts[0]++;} if(report->runs[si].adaptive_tick){delays[1]+=report->runs[si].adaptive_tick;counts[1]++;} }
      if(counts[0]) delays[0]/=counts[0];
      if(counts[1]) delays[1]/=counts[1];
      strcpy(path,charts);strcat(path,"\\detection_delay.svg");write_svg(path,"Delai moyen de detection (ticks)",labels,delays,2); }
    return true;
}
void evaluation_print_report(const EvaluationReport *report) { size_t i,success=0,blocked=0;double loss=0,saved=0;for(i=0;i<report->run_count;++i){success+=report->runs[i].attack_success?1:0;blocked+=report->runs[i].attack_blocked?1:0;loss+=report->runs[i].attack_loss;saved+=report->runs[i].attack_loss-report->runs[i].protected_loss;}puts("\n========== EVALUATION EXPERIMENTALE ==========");printf("Scenarios attaques : %zu | attaques reussies : %zu | attaques bloquees/reduites : %zu\n",report->run_count,success,blocked);printf("Perte economique moyenne : %.3f | perte evitee moyenne : %.3f\n",report->run_count?loss/report->run_count:0,report->run_count?saved/report->run_count:0);printf("Exports et graphiques SVG : %s\n",report->output_path);}
