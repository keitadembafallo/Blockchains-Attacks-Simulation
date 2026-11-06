#ifndef ATELIER2_ANALYSIS_H
#define ATELIER2_ANALYSIS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t samples;
    char campaign_path[512];
} AnalysisReport;

bool analysis_run(const char *campaign_path, AnalysisReport *report);
void analysis_print_report(const AnalysisReport *report);

#endif
