#include "browser.h"
#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *chart_query_strings[] = {
    [CHART_TOP_DOWNLOADS]  = "tophits",
    [CHART_TOP_SCORE]      = "topscore",
    [CHART_FEATURED]       = "featured",
    [CHART_TOP_FAVOURITES] = "top_favourites",
};

static const char *chart_names[] = {
    [CHART_TOP_DOWNLOADS]  = "Most Downloaded",
    [CHART_TOP_SCORE]      = "Most Revered",
    [CHART_FEATURED]       = "Featured",
    [CHART_TOP_FAVOURITES] = "Top Favourites",
};

void browser_init(browser_state_t *state) {
    memset(state, 0, sizeof(*state));
    state->current_chart = CHART_TOP_DOWNLOADS;
    state->current_page = 1;
    state->total_pages = 25;
    state->hover_index = -1;
    state->hover_active = true;
}

const char *browser_chart_name(chart_type_t chart) {
    if (chart >= CHART_COUNT) return "Unknown";
    return chart_names[chart];
}

void browser_get_download_url(const module_entry_t *entry, char *url_buf, size_t buf_size) {
    snprintf(url_buf, buf_size,
             "https://api.modarchive.org/downloads.php?moduleid=%u#%s",
             entry->module_id, entry->filename);
}

static void decode_html_entities(char *str) {
    char *src = str;
    char *dst = str;
    while (*src) {
        if (*src == '&') {
            if (strncmp(src, "&amp;", 5) == 0) { *dst++ = '&'; src += 5; }
            else if (strncmp(src, "&lt;", 4) == 0) { *dst++ = '<'; src += 4; }
            else if (strncmp(src, "&gt;", 4) == 0) { *dst++ = '>'; src += 4; }
            else if (strncmp(src, "&quot;", 6) == 0) { *dst++ = '"'; src += 6; }
            else if (strncmp(src, "&#039;", 6) == 0) { *dst++ = '\''; src += 6; }
            else { *dst++ = *src++; }
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static const char *parse_module_entry(const char *html, module_entry_t *entry) {
    memset(entry, 0, sizeof(*entry));

    const char *rank_start = strstr(html, "chart-listing-title\">\n#");
    if (!rank_start) {
        rank_start = strstr(html, "chart-listing-title\">\n #");
        if (!rank_start) return NULL;
    }
    rank_start = strchr(rank_start, '#');
    if (!rank_start) return NULL;
    entry->rank = atoi(rank_start + 1);

    const char *title_marker = strstr(rank_start, "chart-listing-title\" href=\"module.php?");
    if (!title_marker) return NULL;

    const char *id_start = strstr(title_marker, "module.php?");
    if (!id_start) return NULL;
    id_start += strlen("module.php?");
    entry->module_id = (uint32_t)strtoul(id_start, NULL, 10);

    const char *title_text_start = strchr(id_start, '>');
    if (!title_text_start) return NULL;
    title_text_start++;
    const char *title_text_end = strstr(title_text_start, "</a>");
    if (!title_text_end) return NULL;

    size_t title_len = (size_t)(title_text_end - title_text_start);
    if (title_len >= BROWSER_MAX_TITLE) title_len = BROWSER_MAX_TITLE - 1;
    memcpy(entry->title, title_text_start, title_len);
    entry->title[title_len] = '\0';
    decode_html_entities(entry->title);

    const char *fn_marker = strstr(title_text_end, "<span class=\"chart-listing\">");
    if (!fn_marker) return NULL;
    fn_marker += strlen("<span class=\"chart-listing\">");
    const char *fn_end = strstr(fn_marker, "</span>");
    if (!fn_end) return NULL;

    size_t fn_len = (size_t)(fn_end - fn_marker);
    if (fn_len >= BROWSER_MAX_FILENAME) fn_len = BROWSER_MAX_FILENAME - 1;
    memcpy(entry->filename, fn_marker, fn_len);
    entry->filename[fn_len] = '\0';

    const char *dl_marker = strstr(fn_end, "Downloaded ");
    if (dl_marker) {
        dl_marker += strlen("Downloaded ");
        uint32_t count = 0;
        while (*dl_marker && *dl_marker != ' ') {
            if (isdigit((unsigned char)*dl_marker)) {
                count = count * 10 + (uint32_t)(*dl_marker - '0');
            }
            dl_marker++;
        }
        entry->download_count = count;
    }

    const char *next = strstr(fn_end, "</table>");
    return next ? next + strlen("</table>") : fn_end;
}

static const char *parse_search_entry(const char *html, module_entry_t *entry) {
    memset(entry, 0, sizeof(*entry));

    const char *link = strstr(html, "request=view_by_moduleid&amp;query=");
    if (!link) return NULL;
    link += strlen("request=view_by_moduleid&amp;query=");
    entry->module_id = (uint32_t)strtoul(link, NULL, 10);
    if (entry->module_id == 0) return NULL;

    const char *title_attr = strstr(link, "title=\"");
    if (!title_attr) return NULL;
    title_attr += strlen("title=\"");
    const char *title_end = strchr(title_attr, '"');
    if (!title_end) return NULL;

    const char *fn_start = strchr(title_end, '>');
    if (!fn_start) return NULL;
    fn_start++;
    const char *fn_end = strstr(fn_start, "</a>");
    if (!fn_end) return NULL;

    size_t fn_len = (size_t)(fn_end - fn_start);
    if (fn_len >= BROWSER_MAX_FILENAME) fn_len = BROWSER_MAX_FILENAME - 1;
    memcpy(entry->filename, fn_start, fn_len);
    entry->filename[fn_len] = '\0';

    const char *span = strstr(fn_end, "<span class=\"module-listing\">");
    if (!span) {
        strncpy(entry->title, entry->filename, BROWSER_MAX_TITLE - 1);
    } else {
        span += strlen("<span class=\"module-listing\">");
        while (*span == '\n' || *span == ' ') span++;
        const char *span_end = strstr(span, "</span>");
        if (span_end) {
            const char *t_end = span_end;
            while (t_end > span && (*(t_end-1) == '\n' || *(t_end-1) == ' ')) t_end--;
            size_t t_len = (size_t)(t_end - span);
            if (t_len >= BROWSER_MAX_TITLE) t_len = BROWSER_MAX_TITLE - 1;
            memcpy(entry->title, span, t_len);
            entry->title[t_len] = '\0';
        } else {
            strncpy(entry->title, entry->filename, BROWSER_MAX_TITLE - 1);
        }
    }

    decode_html_entities(entry->title);
    entry->rank = 0;

    const char *next_row = strstr(fn_end, "</tr>");
    return next_row ? next_row + strlen("</tr>") : fn_end + 1;
}

bool browser_search(browser_state_t *state, const char *query, int page) {
    if (!query || !query[0]) return false;

    state->loading = true;
    state->load_error = false;
    state->module_count = 0;
    state->current_page = page;
    state->total_pages = 1;

    char url[512];
    snprintf(url, sizeof(url),
             "https://modarchive.org/index.php?request=search&query=%s&submit=Find&search_type=filename_or_songtitle&page=%d",
             query, page);

    http_response_t response = {0};
    if (!http_fetch_text(url, &response)) {
        state->loading = false;
        state->load_error = true;
        return false;
    }

    const char *last_page = strstr(response.data, "&gt;</a>");
    if (last_page) {
        const char *page_start = last_page;
        while (page_start > response.data && *(page_start - 1) != '=') {
            page_start--;
        }
        int tp = atoi(page_start);
        if (tp > 0) state->total_pages = tp;
    }

    const char *pos = response.data;
    int count = 0;

    while (count < BROWSER_MAX_MODULES) {
        const char *next = parse_search_entry(pos, &state->modules[count]);
        if (!next) break;
        if (state->modules[count].module_id > 0) {
            count++;
        }
        pos = next;
    }

    state->module_count = count;
    state->loading = false;

    free(response.data);
    return count > 0;
}

bool browser_search_artist(artist_result_t *result, const char *query) {
    memset(result, 0, sizeof(*result));
    if (!query || !query[0]) return false;

    char url[512];
    snprintf(url, sizeof(url),
             "https://modarchive.org/index.php?request=search&query=%s&submit=Find&search_type=search_artist",
             query);

    http_response_t response = {0};
    if (!http_fetch_text(url, &response)) return false;

    const char *link = strstr(response.data, "search-result-link");
    if (!link) {
        free(response.data);
        return false;
    }

    const char *href = NULL;
    const char *scan = link;
    while (scan > response.data && *scan != '<') scan--;
    href = strstr(scan, "member.php?");
    if (!href) {
        free(response.data);
        return false;
    }
    href += strlen("member.php?");
    result->artist_id = (uint32_t)strtoul(href, NULL, 10);
    if (result->artist_id == 0) {
        free(response.data);
        return false;
    }

    const char *name_start = strchr(link, '>');
    if (name_start) {
        name_start++;
        const char *name_end = strstr(name_start, "</a>");
        if (name_end) {
            size_t len = (size_t)(name_end - name_start);
            if (len >= BROWSER_MAX_ARTIST_NAME) len = BROWSER_MAX_ARTIST_NAME - 1;
            memcpy(result->artist_name, name_start, len);
            result->artist_name[len] = '\0';
        }
    }

    result->artist_found = true;
    free(response.data);
    return true;
}

static const char *parse_artist_module_entry(const char *html, module_entry_t *entry) {
    memset(entry, 0, sizeof(*entry));

    const char *dl_link = strstr(html, "downloads.php?moduleid=");
    if (!dl_link) return NULL;
    dl_link += strlen("downloads.php?moduleid=");
    entry->module_id = (uint32_t)strtoul(dl_link, NULL, 10);
    if (entry->module_id == 0) return NULL;

    const char *hash = strchr(dl_link, '#');
    if (hash) {
        hash++;
        const char *quote = strchr(hash, '"');
        if (quote) {
            size_t len = (size_t)(quote - hash);
            if (len >= BROWSER_MAX_FILENAME) len = BROWSER_MAX_FILENAME - 1;
            memcpy(entry->filename, hash, len);
            entry->filename[len] = '\0';
        }
    }

    const char *title_span = strstr(dl_link, "<span class=\"module-listing\">");
    if (title_span) {
        title_span += strlen("<span class=\"module-listing\">");
        const char *span_end = strstr(title_span, "</span>");
        if (span_end) {
            while (*title_span == ' ' || *title_span == '\n') title_span++;
            const char *t_end = span_end;
            while (t_end > title_span && (*(t_end-1) == ' ' || *(t_end-1) == '\n')) t_end--;
            size_t len = (size_t)(t_end - title_span);
            if (len >= BROWSER_MAX_TITLE) len = BROWSER_MAX_TITLE - 1;
            memcpy(entry->title, title_span, len);
            entry->title[len] = '\0';
            decode_html_entities(entry->title);
        }
    }

    if (!entry->title[0]) {
        strncpy(entry->title, entry->filename, BROWSER_MAX_TITLE - 1);
    }

    const char *next_row = strstr(dl_link, "</tr>");
    return next_row ? next_row + strlen("</tr>") : dl_link + 1;
}

bool browser_fetch_artist_modules(artist_result_t *result) {
    if (!result->artist_found || result->artist_id == 0) return false;

    char url[512];
    snprintf(url, sizeof(url),
             "https://modarchive.org/index.php?request=view_artist_modules&query=%u",
             result->artist_id);

    http_response_t response = {0};
    if (!http_fetch_text(url, &response)) return false;

    const char *pos = response.data;
    int count = 0;

    while (count < BROWSER_MAX_MODULES) {
        const char *next = parse_artist_module_entry(pos, &result->artist_modules[count]);
        if (!next) break;
        if (result->artist_modules[count].module_id > 0) {
            result->artist_modules[count].rank = count + 1;
            count++;
        }
        pos = next;
    }

    result->artist_module_count = count;
    free(response.data);
    return count > 0;
}

bool browser_fetch_chart(browser_state_t *state, chart_type_t chart, int page) {
    if (chart >= CHART_COUNT) return false;

    state->loading = true;
    state->load_error = false;
    state->module_count = 0;
    state->current_chart = chart;
    state->current_page = page;

    char url[512];
    if (chart == CHART_TOP_FAVOURITES) {
        snprintf(url, sizeof(url),
                 "https://modarchive.org/index.php?request=view_top_favourites&page=%d",
                 page);
    } else {
        snprintf(url, sizeof(url),
                 "https://modarchive.org/index.php?request=view_chart&query=%s&page=%d",
                 chart_query_strings[chart], page);
    }

    http_response_t response = {0};
    if (!http_fetch_text(url, &response)) {
        state->loading = false;
        state->load_error = true;
        return false;
    }

    const char *last_page = strstr(response.data, "&gt;&gt;</a>");
    if (last_page) {
        const char *page_start = last_page;
        while (page_start > response.data && *(page_start - 1) != '=') {
            page_start--;
        }
        int tp = atoi(page_start);
        if (tp > 0) state->total_pages = tp;
    }

    const char *pos = response.data;
    int count = 0;

    while (count < BROWSER_MAX_MODULES) {
        const char *next = parse_module_entry(pos, &state->modules[count]);
        if (!next) break;
        count++;
        pos = next;
    }

    state->module_count = count;
    state->loading = false;

    free(response.data);
    return count > 0;
}
