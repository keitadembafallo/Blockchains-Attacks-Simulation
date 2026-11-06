#ifndef ATELIER2_DETECTION_H
#define ATELIER2_DETECTION_H

#include "metrics.h"

#define DETECTION_MAX_ALERTS 16
#define DETECTION_BASELINE_WINDOW 20
typedef struct {
    size_t count, window;
    double orphan_mean, orphan_std, delay_mean, delay_std, reorg_mean, reorg_std;
    double orphan_threshold, delay_threshold, reorg_threshold;
    bool warmed_up;
} AdaptiveThresholds;
typedef enum { SEVERITY_INFO, SEVERITY_LOW, SEVERITY_MEDIUM, SEVERITY_HIGH, SEVERITY_CRITICAL } DetectionSeverity;
typedef struct {
    char attack[32], suspect[BC_NAME_SIZE], reason[160];
    unsigned score;
    DetectionSeverity severity;
} DetectionAlert;
typedef struct {
    DetectionAlert alerts[DETECTION_MAX_ALERTS];
    size_t alert_count;
    unsigned global_score;
    DetectionSeverity maximum_severity;
    unsigned node_scores[BC_MAX_NODES];
    AdaptiveThresholds adaptive;
} DetectionReport;
void detection_analyze(const MetricsExperiment *experiment, DetectionReport *report);
void detection_analyze_pair(const MetricSummary *reference, const MetricSummary *observed,
                            DetectionReport *report);
void detection_print_report(const DetectionReport *report);
bool detection_export_csv(const DetectionReport *report, const MetricsExperiment *experiment);
bool detection_load_baseline(const char *path, AdaptiveThresholds *thresholds);
bool detection_update_baseline(const char *path, const MetricSummary *normal, size_t window);
const char *detection_severity_name(DetectionSeverity severity);

#endif
