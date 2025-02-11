// SPDX-License-Identifier: GPL-3.0-or-later

#include "charts2json.h"

// generate JSON for the /api/v1/charts API call

const char* get_release_channel() {
    static int use_stable = -1;

    if (use_stable == -1) {
        char filename[FILENAME_MAX + 1];
        snprintfz(filename, FILENAME_MAX, "%s/.environment", netdata_configured_user_config_dir);
        procfile *ff = procfile_open(filename, "=", PROCFILE_FLAG_NO_ERROR_ON_FILE_IO);
        if (ff) {
            procfile_set_quotes(ff, "'\"");
            ff = procfile_readall(ff);
            if (ff) {
                unsigned int i;
                for (i = 0; i < procfile_lines(ff); i++) {
                    if (!procfile_linewords(ff, i))
                        continue;
                    if (!strcmp(procfile_lineword(ff, i, 0), "RELEASE_CHANNEL")) {
                        if (!strcmp(procfile_lineword(ff, i, 1), "stable"))
                            use_stable = 1;
                        else if (!strcmp(procfile_lineword(ff, i, 1), "nightly"))
                            use_stable = 0;
                        break;
                    }
                }
                procfile_close(ff);
            }
        }
        if (use_stable == -1)
            use_stable = strchr(program_version, '-') ? 0 : 1;
    }
    return (use_stable)?"stable":"nightly";
}

void charts2json(RRDHOST *host, BUFFER *wb, int skip_volatile, int show_archived) {
    static char *custom_dashboard_info_js_filename = NULL;
    size_t c, dimensions = 0, memory = 0, alarms = 0;
    RRDSET *st;

    time_t now = now_realtime_sec();

    if(unlikely(!custom_dashboard_info_js_filename))
        custom_dashboard_info_js_filename = config_get(CONFIG_SECTION_WEB, "custom dashboard_info.js", "");

    buffer_sprintf(wb, "{\n"
                       "\t\"hostname\": \"%s\""
                       ",\n\t\"version\": \"%s\""
                       ",\n\t\"release_channel\": \"%s\""
                       ",\n\t\"os\": \"%s\""
                       ",\n\t\"timezone\": \"%s\""
                       ",\n\t\"update_every\": %d"
                       ",\n\t\"history\": %d"
                       ",\n\t\"memory_mode\": \"%s\""
                       ",\n\t\"custom_info\": \"%s\""
                       ",\n\t\"charts\": {"
                   , rrdhost_hostname(host)
                   , rrdhost_program_version(host)
                   , get_release_channel()
                   , rrdhost_os(host)
                   , rrdhost_timezone(host)
                   , host->update_every
                   , host->rrd_history_entries
                   , storage_engine_name(host->storage_engine_id)
                   , custom_dashboard_info_js_filename
    );

    c = 0;
    rrdset_foreach_read(st, host) {
        if ((!show_archived && rrdset_is_available_for_viewers(st)) || (show_archived && rrdset_is_archived(st))) {
            if(c) buffer_strcat(wb, ",");
            buffer_strcat(wb, "\n\t\t\"");
            buffer_strcat(wb, rrdset_id(st));
            buffer_strcat(wb, "\": ");
            rrdset2json(st, wb, &dimensions, &memory, skip_volatile);

            c++;
            st->last_accessed_time_s = now;
        }
    }
    rrdset_foreach_done(st);

    RRDCALC *rc;
    foreach_rrdcalc_in_rrdhost_read(host, rc) {
        if(rc->rrdset)
            alarms++;
    }
    foreach_rrdcalc_in_rrdhost_done(rc);

    buffer_sprintf(wb
                   , "\n\t}"
                     ",\n\t\"charts_count\": %zu"
                     ",\n\t\"dimensions_count\": %zu"
                     ",\n\t\"alarms_count\": %zu"
                     ",\n\t\"rrd_memory_bytes\": %zu"
                     ",\n\t\"hosts_count\": %zu"
                     ",\n\t\"hosts\": ["
                   , c
                   , dimensions
                   , alarms
                   , memory
                   , rrdhost_hosts_available()
    );

    if(unlikely(rrdhost_hosts_available() > 1)) {
        rrd_rdlock();

        size_t found = 0;
        RRDHOST *h;
        rrdhost_foreach_read(h) {
            if(!rrdhost_should_be_removed(h, host, now) && !rrdhost_flag_check(h, RRDHOST_FLAG_ARCHIVED)) {
                buffer_sprintf(wb
                               , "%s\n\t\t{"
                                 "\n\t\t\t\"hostname\": \"%s\""
                                 "\n\t\t}"
                               , (found > 0) ? "," : ""
                               , rrdhost_hostname(h)
                );

                found++;
            }
        }

        rrd_unlock();
    }
    else {
        buffer_sprintf(wb
                       , "\n\t\t{"
                         "\n\t\t\t\"hostname\": \"%s\""
                         "\n\t\t}"
                       , rrdhost_hostname(host)
        );
    }

    buffer_sprintf(wb, "\n\t]\n}\n");
}
