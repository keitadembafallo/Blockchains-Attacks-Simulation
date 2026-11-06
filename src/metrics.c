#include "metrics.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

static unsigned long run_counter = 0;
static int make_dir(const char *path) {
#ifdef _WIN32
    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return mkdir(path, 0777) == 0 || errno == EEXIST;
#endif
}
static void add_event(MetricSummary *summary, const char *mode, const char *type, const char *detail,
                      size_t tick, size_t delay) {
    MetricEvent *event;
    if (summary->event_count == METRICS_MAX_EVENTS) return;
    event = &summary->events[summary->event_count++];
    memset(event, 0, sizeof(*event));
    snprintf(event->mode, sizeof(event->mode), "%s", mode);
    snprintf(event->type, sizeof(event->type), "%s", type);
    snprintf(event->detail, sizeof(event->detail), "%s", detail);
    event->tick = tick; event->delay_ticks = delay;
    if (delay > summary->maximum_delay) summary->maximum_delay = (double)delay;
    summary->average_delay += (double)delay;
}
void metrics_summarize_chain(const Blockchain *chain, MetricSummary *summary) {
    size_t i;
    if (chain == NULL || summary == NULL) return;
    memset(summary, 0, sizeof(*summary));
    summary->blocks_produced = chain->block_count > 0 ? chain->block_count - 1 : 0;
    for (i = 0; i < chain->node_count; ++i) summary->total_rewards += chain->nodes[i].rewards;
}
bool metrics_run_experiment(const Blockchain *base, const AttackScenario *scenario, MetricsExperiment *experiment) {
    size_t i;
    if (base == NULL || scenario == NULL || experiment == NULL) return false;
    memset(experiment, 0, sizeof(*experiment));
    experiment->scenario = *scenario;
    if (!attack_run(base, scenario, &experiment->attack)) return false;
    metrics_summarize_chain(&experiment->attack.reference_chain, &experiment->normal);
    metrics_summarize_chain(&experiment->attack.attacked_chain, &experiment->malicious);
    experiment->normal.public_blocks = experiment->normal.blocks_produced;
    experiment->malicious.public_blocks = experiment->attack.public_blocks;
    experiment->malicious.private_blocks = experiment->attack.private_blocks;
    experiment->malicious.withheld_blocks = scenario->type == ATTACK_BLOCK_WITHHOLDING ?
                                            experiment->attack.private_blocks : 0;
    experiment->malicious.orphaned_blocks = experiment->attack.orphaned_blocks;
    experiment->malicious.reorganization_depth = experiment->attack.reorganization_depth;
    experiment->malicious.double_spends = experiment->attack.merchant_loss > 0 ? 1 : 0;
    experiment->malicious.sybil_identities = scenario->type == ATTACK_SYBIL ? scenario->sybil_count : 0;
    experiment->malicious.sybil_neighbors = experiment->attack.sybil_neighbors;
    experiment->malicious.attacker_rewards = experiment->attack.attacker_rewards;
    experiment->malicious.orphan_rate = experiment->malicious.blocks_produced ?
        (double)experiment->malicious.orphaned_blocks / experiment->malicious.blocks_produced : 0.0;
    experiment->normal.invalid_chains = experiment->attack.reference_valid ? 0 : 1;
    experiment->malicious.invalid_chains = experiment->attack.attacked_valid ? 0 : 1;
    for (i = 0; i < experiment->attack.trace_count; ++i) {
        size_t base_delay = scenario->propagation_delay ? scenario->propagation_delay : 1;
        size_t delay = scenario->type == ATTACK_SYBIL ? base_delay + 1 : base_delay;
        add_event(&experiment->malicious, "ATTAQUE", attack_type_name(scenario->type),
                  experiment->attack.trace[i], i + 1, delay);
        add_event(&experiment->normal, "NORMAL", "BLOC_HONNET", "Production honnete de reference.", i + 1, base_delay);
    }
    if (experiment->malicious.event_count) experiment->malicious.average_delay /=
        experiment->malicious.event_count;
    if (experiment->normal.event_count) experiment->normal.average_delay /= experiment->normal.event_count;
    snprintf(experiment->run_id, sizeof(experiment->run_id), "run_%06lu", ++run_counter);
    return true;
}
void metrics_print_experiment(const MetricsExperiment *e) {
    puts("\n========== METRIQUES : NORMAL / ATTAQUE ==========");
    if (e->protected_available) {
        puts("Indicateur                    NORMAL       ATTAQUE      PROTEGE");
        printf("Blocs produits                %-12zu %-12zu %-12zu\n", e->normal.blocks_produced, e->malicious.blocks_produced, e->protected.blocks_produced);
        printf("Blocs prives / retenus        %-12zu %-12zu %-12zu\n", e->normal.private_blocks, e->malicious.private_blocks, e->protected.private_blocks);
        printf("Blocs orphelins               %-12zu %-12zu %-12zu\n", e->normal.orphaned_blocks, e->malicious.orphaned_blocks, e->protected.orphaned_blocks);
        printf("Taux orphelins                %-12.3f %-12.3f %-12.3f\n", e->normal.orphan_rate, e->malicious.orphan_rate, e->protected.orphan_rate);
        printf("Delai moyen (ticks)           %-12.3f %-12.3f %-12.3f\n", e->normal.average_delay, e->malicious.average_delay, e->protected.average_delay);
        printf("Reorganisation                %-12zu %-12zu %-12zu\n", e->normal.reorganization_depth, e->malicious.reorganization_depth, e->protected.reorganization_depth);
        printf("Recompenses totales           %-12" PRIu64 " %-12" PRIu64 " %-12" PRIu64 "\n", e->normal.total_rewards, e->malicious.total_rewards, e->protected.total_rewards);
        printf("Recompenses attaquantes       %-12" PRIu64 " %-12" PRIu64 " %-12" PRIu64 "\n", e->normal.attacker_rewards, e->malicious.attacker_rewards, e->protected.attacker_rewards);
    } else {
        puts("Indicateur                    NORMAL       ATTAQUE");
        printf("Blocs produits                %-12zu %-12zu\n", e->normal.blocks_produced, e->malicious.blocks_produced);
        printf("Blocs prives / retenus        %-12zu %-12zu\n", e->normal.private_blocks, e->malicious.private_blocks);
        printf("Blocs orphelins               %-12zu %-12zu\n", e->normal.orphaned_blocks, e->malicious.orphaned_blocks);
        printf("Taux orphelins                %-12.3f %-12.3f\n", e->normal.orphan_rate, e->malicious.orphan_rate);
        printf("Delai moyen (ticks)           %-12.3f %-12.3f\n", e->normal.average_delay, e->malicious.average_delay);
        printf("Reorganisation                %-12zu %-12zu\n", e->normal.reorganization_depth, e->malicious.reorganization_depth);
        printf("Recompenses totales           %-12" PRIu64 " %-12" PRIu64 "\n", e->normal.total_rewards, e->malicious.total_rewards);
        printf("Recompenses attaquantes       %-12" PRIu64 " %-12" PRIu64 "\n", e->normal.attacker_rewards, e->malicious.attacker_rewards);
    }
}
bool metrics_default_directory(char output[METRICS_PATH_SIZE]) {
#ifdef _WIN32
    char executable[METRICS_PATH_SIZE], *slash; DWORD n=GetModuleFileNameA(NULL,executable,sizeof(executable));
    if(!n || n>=sizeof(executable) || (slash=strrchr(executable,'\\'))==NULL) return false;
    *slash='\0'; return snprintf(output,METRICS_PATH_SIZE,"%s\\data\\experiments",executable)<METRICS_PATH_SIZE;
#else
    return snprintf(output,METRICS_PATH_SIZE,"data/experiments")<METRICS_PATH_SIZE;
#endif
}
static bool write_summary(const char *path, const MetricsExperiment *e) {
    FILE *f=fopen(path,"w"); if(!f) return false;
    fprintf(f,"mode,blocks,public,private,withheld,orphaned,orphan_rate,avg_delay,max_delay,reorg,invalid,double_spend,sybils,rewards,attacker_rewards\n");
    fprintf(f,"NORMAL,%zu,%zu,%zu,%zu,%zu,%.6f,%.6f,%.6f,%zu,%zu,%zu,%zu,%" PRIu64 ",%" PRIu64 "\n",
      e->normal.blocks_produced,e->normal.public_blocks,e->normal.private_blocks,e->normal.withheld_blocks,e->normal.orphaned_blocks,e->normal.orphan_rate,e->normal.average_delay,e->normal.maximum_delay,e->normal.reorganization_depth,e->normal.invalid_chains,e->normal.double_spends,e->normal.sybil_identities,e->normal.total_rewards,e->normal.attacker_rewards);
    fprintf(f,"ATTAQUE,%zu,%zu,%zu,%zu,%zu,%.6f,%.6f,%.6f,%zu,%zu,%zu,%zu,%" PRIu64 ",%" PRIu64 "\n",
      e->malicious.blocks_produced,e->malicious.public_blocks,e->malicious.private_blocks,e->malicious.withheld_blocks,e->malicious.orphaned_blocks,e->malicious.orphan_rate,e->malicious.average_delay,e->malicious.maximum_delay,e->malicious.reorganization_depth,e->malicious.invalid_chains,e->malicious.double_spends,e->malicious.sybil_identities,e->malicious.total_rewards,e->malicious.attacker_rewards);
    if (e->protected_available) fprintf(f,"PROTEGE,%zu,%zu,%zu,%zu,%zu,%.6f,%.6f,%.6f,%zu,%zu,%zu,%zu,%" PRIu64 ",%" PRIu64 "\n",
      e->protected.blocks_produced,e->protected.public_blocks,e->protected.private_blocks,e->protected.withheld_blocks,e->protected.orphaned_blocks,e->protected.orphan_rate,e->protected.average_delay,e->protected.maximum_delay,e->protected.reorganization_depth,e->protected.invalid_chains,e->protected.double_spends,e->protected.sybil_identities,e->protected.total_rewards,e->protected.attacker_rewards);
    return fclose(f)==0;
}
bool metrics_export_csv(MetricsExperiment *e, const char *directory) {
    char run[METRICS_PATH_SIZE], path[METRICS_PATH_SIZE + 32], parent[METRICS_PATH_SIZE], *slash; FILE *f; size_t i, mode;
    if (!e || !directory || strlen(directory) >= sizeof(parent)) return false;
    snprintf(parent,sizeof(parent),"%s",directory); slash=strrchr(parent,'\\');
    if (slash != NULL) { *slash='\0'; if (!make_dir(parent)) return false; }
    if (!make_dir(directory) || snprintf(run,sizeof(run),"%s\\%s",directory,e->run_id)>=(int)sizeof(run) || !make_dir(run)) return false;
    snprintf(e->export_path,sizeof(e->export_path),"%s",run);
    snprintf(path,sizeof(path),"%s\\summary.csv",run); if(!write_summary(path,e)) return false;
    snprintf(path,sizeof(path),"%s\\events.csv",run); f=fopen(path,"w"); if(!f)return false;
    fprintf(f,"mode,tick,type,node,branch,height,delay_ticks,amount,detail\n");
    for(mode=0;mode<2;++mode){const MetricSummary*s=mode?&e->malicious:&e->normal;for(i=0;i<s->event_count;++i)fprintf(f,"%s,%zu,%s,%s,%s,%zu,%zu,%" PRIu64 ",%s\n",s->events[i].mode,s->events[i].tick,s->events[i].type,s->events[i].node,s->events[i].branch,s->events[i].height,s->events[i].delay_ticks,s->events[i].amount,s->events[i].detail);}
    if(fclose(f)!=0)return false;
    snprintf(path,sizeof(path),"%s\\nodes.csv",run); f=fopen(path,"w"); if(!f)return false;
    fprintf(f,"mode,node,rewards,hash_power,stake\n");
    for(mode=0;mode<2;++mode){const Blockchain*c=mode?&e->attack.attacked_chain:&e->attack.reference_chain;for(i=0;i<c->node_count;++i)fprintf(f,"%s,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",mode?"ATTAQUE":"NORMAL",c->nodes[i].name,c->nodes[i].rewards,c->nodes[i].hash_power,c->nodes[i].stake);}
    fclose(f);
    snprintf(path,sizeof(path),"%s\\scenario.csv",run); f=fopen(path,"w"); if(!f)return false;
    fprintf(f,"run_id,attack,consensus,seed,rounds,success,mitigation_applied,adoption_blocked\n%s,%s,%s,%" PRIu64 ",%zu,%d,%d,%d\n",e->run_id,
      attack_type_name(e->scenario.type),bc_consensus_name(e->scenario.consensus),e->scenario.seed,
      e->scenario.rounds,e->attack.success?1:0,e->mitigation_applied?1:0,e->adoption_blocked?1:0); fclose(f);
    return true;
}
