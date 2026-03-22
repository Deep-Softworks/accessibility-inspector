#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ApplicationServices/ApplicationServices.h>

#define DEFAULT_DEPTH    20
#define MAX_GUIDES       256
#define MAX_ROLE_COUNTS  512

/* ── Options ─────────────────────────────────────────────────────── */

typedef struct {
    const char *app_name;
    int   depth;
    int   focus_only;
    int   show_values;
    int   show_geometry;
    int   show_actions;
    int   show_all_attrs;
    const char *role_filter;   /* NULL = no filter */
    const char *search_text;   /* NULL = no search */
    int   json;
    int   count;
    int   list_apps;
} Options;

/* ── Tree node ───────────────────────────────────────────────────── */

typedef struct Node {
    char  *role;
    char  *value;               /* NULL if unavailable */
    int    has_selected_range;
    long   sel_location, sel_length;
    int    has_position;
    double pos_x, pos_y;
    int    has_size;
    double size_w, size_h;
    char **actions;
    int    num_actions;
    char **attrs;
    int    num_attrs;
    struct Node *first_child;
    struct Node *next_sibling;
    int    search_match;        /* used by --search marking pass */
} Node;

/* ── Cached CFString attribute names ─────────────────────────────── */

static CFStringRef cf_attr(const char *name) {
    /* Simple; not cached since called infrequently per element. */
    return CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);
}

/* ── CFString → malloc'd C string ────────────────────────────────── */

static char *cfstring_to_cstring(CFStringRef ref) {
    if (!ref) return NULL;
    CFIndex len = CFStringGetLength(ref);
    CFIndex max = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    char *buf = malloc((size_t)max);
    if (!buf) return NULL;
    if (!CFStringGetCString(ref, buf, max, kCFStringEncodingUTF8)) {
        free(buf);
        return NULL;
    }
    return buf;
}

/* ── CFType → malloc'd string (string / bool / number) ───────────── */

static char *cftype_to_string(CFTypeRef val) {
    if (!val) return NULL;
    CFTypeID tid = CFGetTypeID(val);

    if (tid == CFStringGetTypeID())
        return cfstring_to_cstring((CFStringRef)val);

    if (tid == CFBooleanGetTypeID()) {
        Boolean b = CFBooleanGetValue((CFBooleanRef)val);
        return strdup(b ? "true" : "false");
    }

    if (tid == CFNumberGetTypeID()) {
        long long iv;
        if (CFNumberGetValue((CFNumberRef)val, kCFNumberLongLongType, &iv)) {
            char buf[64];
            snprintf(buf, sizeof buf, "%lld", iv);
            return strdup(buf);
        }
        double dv;
        if (CFNumberGetValue((CFNumberRef)val, kCFNumberDoubleType, &dv)) {
            char buf[64];
            snprintf(buf, sizeof buf, "%g", dv);
            return strdup(buf);
        }
    }

    return NULL;
}

/* ── AX helpers ──────────────────────────────────────────────────── */

static CFTypeRef ax_get_attr(AXUIElementRef el, CFStringRef attr) {
    CFTypeRef val = NULL;
    AXError err = AXUIElementCopyAttributeValue(el, attr, &val);
    if (err != kAXErrorSuccess) return NULL;
    return val;
}

static int ax_has_attr(CFArrayRef names, const char *name) {
    if (!names) return 0;
    CFStringRef key = cf_attr(name);
    CFIndex n = CFArrayGetCount(names);
    for (CFIndex i = 0; i < n; i++) {
        CFStringRef s = (CFStringRef)CFArrayGetValueAtIndex(names, i);
        if (CFStringCompare(s, key, 0) == kCFCompareEqualTo) {
            CFRelease(key);
            return 1;
        }
    }
    CFRelease(key);
    return 0;
}

static char *ax_get_role(AXUIElementRef el) {
    CFStringRef attr = cf_attr("AXRole");
    CFTypeRef val = ax_get_attr(el, attr);
    CFRelease(attr);
    if (!val) return NULL;
    if (CFGetTypeID(val) != CFStringGetTypeID()) { CFRelease(val); return NULL; }
    char *s = cfstring_to_cstring((CFStringRef)val);
    CFRelease(val);
    return s;
}

static char *ax_get_value(AXUIElementRef el) {
    CFStringRef attr = cf_attr("AXValue");
    CFTypeRef val = ax_get_attr(el, attr);
    CFRelease(attr);
    if (!val) return NULL;
    char *s = cftype_to_string(val);
    CFRelease(val);
    return s;
}

static int ax_get_selected_range(AXUIElementRef el, long *loc, long *len) {
    CFStringRef attr = cf_attr("AXSelectedTextRange");
    CFTypeRef val = ax_get_attr(el, attr);
    CFRelease(attr);
    if (!val) return 0;
    if (CFGetTypeID(val) != AXValueGetTypeID()) { CFRelease(val); return 0; }
    if (AXValueGetType((AXValueRef)val) != kAXValueTypeCFRange) { CFRelease(val); return 0; }
    CFRange range;
    if (!AXValueGetValue((AXValueRef)val, kAXValueTypeCFRange, &range)) { CFRelease(val); return 0; }
    CFRelease(val);
    *loc = (long)range.location;
    *len = (long)range.length;
    return 1;
}

static int ax_get_position(AXUIElementRef el, double *x, double *y) {
    CFStringRef attr = cf_attr("AXPosition");
    CFTypeRef val = ax_get_attr(el, attr);
    CFRelease(attr);
    if (!val) return 0;
    if (CFGetTypeID(val) != AXValueGetTypeID()) { CFRelease(val); return 0; }
    if (AXValueGetType((AXValueRef)val) != kAXValueTypeCGPoint) { CFRelease(val); return 0; }
    CGPoint pt;
    if (!AXValueGetValue((AXValueRef)val, kAXValueTypeCGPoint, &pt)) { CFRelease(val); return 0; }
    CFRelease(val);
    *x = pt.x; *y = pt.y;
    return 1;
}

static int ax_get_size(AXUIElementRef el, double *w, double *h) {
    CFStringRef attr = cf_attr("AXSize");
    CFTypeRef val = ax_get_attr(el, attr);
    CFRelease(attr);
    if (!val) return 0;
    if (CFGetTypeID(val) != AXValueGetTypeID()) { CFRelease(val); return 0; }
    if (AXValueGetType((AXValueRef)val) != kAXValueTypeCGSize) { CFRelease(val); return 0; }
    CGSize sz;
    if (!AXValueGetValue((AXValueRef)val, kAXValueTypeCGSize, &sz)) { CFRelease(val); return 0; }
    CFRelease(val);
    *w = sz.width; *h = sz.height;
    return 1;
}

static int ax_get_actions(AXUIElementRef el, char ***out_actions) {
    CFArrayRef actions = NULL;
    AXError err = AXUIElementCopyActionNames(el, &actions);
    if (err != kAXErrorSuccess || !actions) return 0;
    CFIndex n = CFArrayGetCount(actions);
    if (n == 0) { CFRelease(actions); return 0; }
    char **arr = malloc(sizeof(char *) * (size_t)n);
    int count = 0;
    for (CFIndex i = 0; i < n; i++) {
        char *s = cfstring_to_cstring((CFStringRef)CFArrayGetValueAtIndex(actions, i));
        if (s) arr[count++] = s;
    }
    CFRelease(actions);
    *out_actions = arr;
    return count;
}

static int ax_get_all_attr_names(AXUIElementRef el, char ***out_attrs) {
    CFArrayRef names = NULL;
    AXError err = AXUIElementCopyAttributeNames(el, &names);
    if (err != kAXErrorSuccess || !names) return 0;
    CFIndex n = CFArrayGetCount(names);
    if (n == 0) { CFRelease(names); return 0; }
    char **arr = malloc(sizeof(char *) * (size_t)n);
    int count = 0;
    for (CFIndex i = 0; i < n; i++) {
        char *s = cfstring_to_cstring((CFStringRef)CFArrayGetValueAtIndex(names, i));
        if (s) arr[count++] = s;
    }
    CFRelease(names);
    *out_attrs = arr;
    return count;
}

/* ── Tree building ───────────────────────────────────────────────── */

static Node *build_node(AXUIElementRef el, int depth, const Options *opts) {
    if (depth > opts->depth) return NULL;

    CFArrayRef attr_names = NULL;
    AXUIElementCopyAttributeNames(el, &attr_names);
    if (!ax_has_attr(attr_names, "AXRole")) {
        if (attr_names) CFRelease(attr_names);
        return NULL;
    }

    char *role = ax_get_role(el);
    if (!role) { if (attr_names) CFRelease(attr_names); return NULL; }

    Node *node = calloc(1, sizeof(Node));
    node->role = role;

    if (ax_has_attr(attr_names, "AXValue"))
        node->value = ax_get_value(el);

    if (ax_has_attr(attr_names, "AXSelectedTextRange"))
        node->has_selected_range = ax_get_selected_range(el, &node->sel_location, &node->sel_length);

    if (opts->show_geometry) {
        if (ax_has_attr(attr_names, "AXPosition"))
            node->has_position = ax_get_position(el, &node->pos_x, &node->pos_y);
        if (ax_has_attr(attr_names, "AXSize"))
            node->has_size = ax_get_size(el, &node->size_w, &node->size_h);
    }

    if (opts->show_actions)
        node->num_actions = ax_get_actions(el, &node->actions);

    if (opts->show_all_attrs)
        node->num_attrs = ax_get_all_attr_names(el, &node->attrs);

    if (depth >= opts->depth || !ax_has_attr(attr_names, "AXChildren")) {
        if (attr_names) CFRelease(attr_names);
        return node;
    }

    if (attr_names) CFRelease(attr_names);

    /* Fetch children */
    CFStringRef children_key = cf_attr("AXChildren");
    CFIndex count = 0;
    AXUIElementGetAttributeValueCount(el, children_key, &count);
    if (count <= 0) { CFRelease(children_key); return node; }

    CFArrayRef children = NULL;
    AXUIElementCopyAttributeValues(el, children_key, 0, count, &children);
    CFRelease(children_key);
    if (!children) return node;

    Node *prev = NULL;
    for (CFIndex i = 0; i < CFArrayGetCount(children); i++) {
        AXUIElementRef child_el = (AXUIElementRef)CFArrayGetValueAtIndex(children, i);
        Node *child = build_node(child_el, depth + 1, opts);
        if (child) {
            if (!node->first_child) node->first_child = child;
            else prev->next_sibling = child;
            prev = child;
        }
    }
    CFRelease(children);
    return node;
}

static Node *build_tree(AXUIElementRef root, const Options *opts) {
    return build_node(root, 0, opts);
}

/* ── Search marking (for --search) ───────────────────────────────── */

static int mark_search(Node *node, const char *text) {
    if (!node) return 0;
    int match = 0;
    if (node->value && strcasestr(node->value, text))
        match = 1;
    for (Node *c = node->first_child; c; c = c->next_sibling) {
        if (mark_search(c, text))
            match = 1;
    }
    node->search_match = match;
    return match;
}

/* ── Case-insensitive substring check ────────────────────────────── */

static int role_matches(const char *role, const char *pattern) {
    return strcasestr(role, pattern) != NULL;
}

/* ── Should we print this node given filters? ────────────────────── */

static int node_visible(const Node *node, const Options *opts) {
    if (opts->role_filter && !role_matches(node->role, opts->role_filter))
        return 0;
    if (opts->search_text && !node->search_match)
        return 0;
    return 1;
}

/* ── Tree printing ────────────────────────────────────────── */

static void write_indent(int *guides, int depth) {
    for (int i = 0; i < depth; i++)
        printf("%s", guides[i] ? "\xe2\x94\x82   " : "    ");
}

static void print_attr_indent(int *guides, int depth, int is_last, int is_root) {
    if (is_root) { printf("    "); return; }
    write_indent(guides, depth);
    printf("%s", is_last ? "    " : "\xe2\x94\x82   ");
}

static void print_node_attrs(const Node *node, int *guides, int depth,
                              int is_last, int is_root, const Options *opts) {
    if (opts->show_values && node->value) {
        print_attr_indent(guides, depth, is_last, is_root);
        printf("value=\"%s\"\n", node->value);
    }
    if (node->has_selected_range) {
        print_attr_indent(guides, depth, is_last, is_root);
        printf("selectedRange={%ld,%ld}\n", node->sel_location, node->sel_length);
    }
    if (opts->show_geometry) {
        if (node->has_position) {
            print_attr_indent(guides, depth, is_last, is_root);
            printf("position=(%.0f, %.0f)\n", node->pos_x, node->pos_y);
        }
        if (node->has_size) {
            print_attr_indent(guides, depth, is_last, is_root);
            printf("size=(%.0f x %.0f)\n", node->size_w, node->size_h);
        }
    }
    if (opts->show_actions && node->num_actions > 0) {
        print_attr_indent(guides, depth, is_last, is_root);
        printf("actions=[");
        for (int i = 0; i < node->num_actions; i++) {
            if (i > 0) printf(", ");
            printf("%s", node->actions[i]);
        }
        printf("]\n");
    }
    if (opts->show_all_attrs && node->num_attrs > 0) {
        print_attr_indent(guides, depth, is_last, is_root);
        printf("attributes=[");
        for (int i = 0; i < node->num_attrs; i++) {
            if (i > 0) printf(", ");
            printf("%s", node->attrs[i]);
        }
        printf("]\n");
    }
}

static void print_children(const Node *child, int *guides, int depth, const Options *opts);

static void print_tree_node(const Node *node, int *guides, int depth,
                             int is_last, const Options *opts) {
    if (node_visible(node, opts)) {
        write_indent(guides, depth);
        printf("%s%s\n",
               is_last ? "\xe2\x94\x94\xe2\x94\x80 " : "\xe2\x94\x9c\xe2\x94\x80 ",
               node->role);
        print_node_attrs(node, guides, depth, is_last, 0, opts);
        guides[depth] = !is_last;
        print_children(node->first_child, guides, depth + 1, opts);
    } else {
        /* Skip this node but still recurse into children at same depth */
        print_children(node->first_child, guides, depth, opts);
    }
}

static void print_children(const Node *child, int *guides, int depth, const Options *opts) {
    const Node *cur = child;
    while (cur) {
        const Node *next = cur->next_sibling;
        int is_last = (next == NULL);
        if (opts->role_filter) {
            const Node *peek = next;
            while (peek && !node_visible(peek, opts)) {
                peek = peek->next_sibling;
            }
            is_last = (peek == NULL);
        }
        print_tree_node(cur, guides, depth, is_last, opts);
        cur = next;
    }
}

static void print_tree(const Node *root, const Options *opts) {
    int guides[MAX_GUIDES] = {0};

    if (node_visible(root, opts)) {
        printf("%s\n", root->role);
        print_node_attrs(root, guides, 0, 1, 1, opts);
        print_children(root->first_child, guides, 0, opts);
    } else {
        print_children(root->first_child, guides, 0, opts);
    }
}

/* ── JSON output ─────────────────────────────────────────────────── */

static void json_print_escaped(const char *s) {
    putchar('"');
    for (; *s; s++) {
        switch (*s) {
        case '"':  printf("\\\""); break;
        case '\\': printf("\\\\"); break;
        case '\b': printf("\\b");  break;
        case '\f': printf("\\f");  break;
        case '\n': printf("\\n");  break;
        case '\r': printf("\\r");  break;
        case '\t': printf("\\t");  break;
        default:
            if ((unsigned char)*s < 0x20)
                printf("\\u%04x", (unsigned char)*s);
            else
                putchar(*s);
        }
    }
    putchar('"');
}

static void json_indent(int depth) {
    for (int i = 0; i < depth * 2; i++) putchar(' ');
}

static void print_json_node(const Node *node, int depth, const Options *opts) {
    if (!node_visible(node, opts)) return;

    json_indent(depth);
    printf("{\n");

    json_indent(depth + 1);
    printf("\"role\": ");
    json_print_escaped(node->role);

    if (opts->show_values && node->value) {
        printf(",\n");
        json_indent(depth + 1);
        printf("\"value\": ");
        json_print_escaped(node->value);
    }

    if (node->has_selected_range) {
        printf(",\n");
        json_indent(depth + 1);
        printf("\"selectedRange\": {\"location\": %ld, \"length\": %ld}",
               node->sel_location, node->sel_length);
    }

    if (opts->show_geometry) {
        if (node->has_position) {
            printf(",\n");
            json_indent(depth + 1);
            printf("\"position\": {\"x\": %.1f, \"y\": %.1f}", node->pos_x, node->pos_y);
        }
        if (node->has_size) {
            printf(",\n");
            json_indent(depth + 1);
            printf("\"size\": {\"width\": %.1f, \"height\": %.1f}", node->size_w, node->size_h);
        }
    }

    if (opts->show_actions && node->num_actions > 0) {
        printf(",\n");
        json_indent(depth + 1);
        printf("\"actions\": [");
        for (int i = 0; i < node->num_actions; i++) {
            if (i > 0) printf(", ");
            json_print_escaped(node->actions[i]);
        }
        printf("]");
    }

    if (opts->show_all_attrs && node->num_attrs > 0) {
        printf(",\n");
        json_indent(depth + 1);
        printf("\"attributes\": [");
        for (int i = 0; i < node->num_attrs; i++) {
            if (i > 0) printf(", ");
            json_print_escaped(node->attrs[i]);
        }
        printf("]");
    }

    int has_children = 0;
    for (Node *c = node->first_child; c; c = c->next_sibling) {
        if (node_visible(c, opts)) { has_children = 1; break; }
    }

    if (has_children) {
        printf(",\n");
        json_indent(depth + 1);
        printf("\"children\": [\n");
        int first = 1;
        for (Node *c = node->first_child; c; c = c->next_sibling) {
            if (!node_visible(c, opts)) continue;
            if (!first) printf(",\n");
            first = 0;
            print_json_node(c, depth + 2, opts);
        }
        printf("\n");
        json_indent(depth + 1);
        printf("]");
    }

    printf("\n");
    json_indent(depth);
    printf("}");
}

static void print_json(const Node *root, const Options *opts) {
    print_json_node(root, 0, opts);
    printf("\n");
}

/* ── Count mode ──────────────────────────────────────────────────── */

typedef struct { char *role; int count; } RoleCount;
static RoleCount role_counts[MAX_ROLE_COUNTS];
static int num_role_counts = 0;

static void count_node(const Node *node) {
    if (!node) return;
    for (int i = 0; i < num_role_counts; i++) {
        if (strcmp(role_counts[i].role, node->role) == 0) {
            role_counts[i].count++;
            goto recurse;
        }
    }
    if (num_role_counts < MAX_ROLE_COUNTS) {
        role_counts[num_role_counts].role = node->role;
        role_counts[num_role_counts].count = 1;
        num_role_counts++;
    }
recurse:
    for (Node *c = node->first_child; c; c = c->next_sibling)
        count_node(c);
}

static int cmp_role_count(const void *a, const void *b) {
    return ((const RoleCount *)b)->count - ((const RoleCount *)a)->count;
}

static void print_count(const Node *root) {
    num_role_counts = 0;
    count_node(root);
    qsort(role_counts, (size_t)num_role_counts, sizeof(RoleCount), cmp_role_count);

    int total = 0;
    for (int i = 0; i < num_role_counts; i++)
        total += role_counts[i].count;

    printf("Element type statistics (%d total):\n\n", total);
    for (int i = 0; i < num_role_counts; i++)
        printf("  %-30s %d\n", role_counts[i].role, role_counts[i].count);
}

/* ── List apps ───────────────────────────────────────────────────── */

typedef struct { pid_t pid; char name[256]; } AppEntry;

static void list_apps(void) {
    FILE *fp = popen("ps -axo pid=,comm=", "r");
    if (!fp) { fprintf(stderr, "Failed to run ps\n"); return; }

    AppEntry apps[1024];
    int count = 0;
    char line[1024];

    while (fgets(line, sizeof line, fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) continue;

        char *end;
        long pid = strtol(p, &end, 10);
        if (end == p) continue;
        p = end;
        while (*p == ' ' || *p == '\t') p++;

        size_t len = strlen(p);
        while (len > 0 && (p[len-1] == '\n' || p[len-1] == '\r')) p[--len] = '\0';
        if (len == 0) continue;

        const char *name = NULL;
        char namebuf[256];
        char *marker = strstr(p, ".app/Contents/MacOS/");
        if (marker) {
            *marker = '\0'; /* temporarily truncate at .app */
            /* Now p is path up to the .app part, e.g. /Applications/Safari */
            char *slash = strrchr(p, '/');
            name = slash ? slash + 1 : p;
        } else {
            char *slash = strrchr(p, '/');
            name = slash ? slash + 1 : p;
        }

        /* Deduplicate */
        int dup = 0;
        for (int i = 0; i < count; i++) {
            if (strcasecmp(apps[i].name, name) == 0) { dup = 1; break; }
        }
        if (dup) continue;

        /* Check if this process responds to accessibility (quick check) */
        AXUIElementRef el = AXUIElementCreateApplication((pid_t)pid);
        if (el) {
            CFArrayRef attr_names = NULL;
            AXError err = AXUIElementCopyAttributeNames(el, &attr_names);
            CFRelease(el);
            if (err != kAXErrorSuccess || !attr_names) continue;
            CFRelease(attr_names);
        } else {
            continue;
        }

        if (count < 1024) {
            apps[count].pid = (pid_t)pid;
            snprintf(apps[count].name, sizeof namebuf, "%s", name);
            count++;
        }
    }
    pclose(fp);

    printf("%-8s %s\n", "PID", "Application");
    printf("%-8s %s\n", "───", "───────────");
    for (int i = 0; i < count; i++)
        printf("%-8d %s\n", apps[i].pid, apps[i].name);
    printf("\n%d application(s) found\n", count);
}

/* ── PID resolution ──────────────────────────────────────────────── */

static pid_t resolve_with_pgrep(const char *app_name) {
    char cmd[512];
    snprintf(cmd, sizeof cmd, "pgrep -ix '%s'", app_name);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    char line[64];
    pid_t pid = -1;
    if (fgets(line, sizeof line, fp)) {
        pid = (pid_t)atoi(line);
        if (pid <= 0) pid = -1;
    }
    pclose(fp);
    return pid;
}

static pid_t resolve_with_ps(const char *app_name) {
    FILE *fp = popen("ps -axo pid=,comm=", "r");
    if (!fp) return -1;

    char line[1024];
    pid_t result = -1;

    while (fgets(line, sizeof line, fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        char *end;
        long pid = strtol(p, &end, 10);
        if (end == p) continue;
        p = end;
        while (*p == ' ' || *p == '\t') p++;

        size_t len = strlen(p);
        while (len > 0 && (p[len-1] == '\n' || p[len-1] == '\r')) p[--len] = '\0';
        if (len == 0) continue;

        /* Extract process name */
        const char *name;
        char *marker = strstr(p, ".app/Contents/MacOS/");
        if (marker) {
            char saved = *marker;
            *marker = '\0';
            char *slash = strrchr(p, '/');
            name = slash ? slash + 1 : p;
            *marker = saved;
        } else {
            char *slash = strrchr(p, '/');
            name = slash ? slash + 1 : p;
            /* Strip extension */
        }

        if (strcasecmp(name, app_name) == 0) {
            result = (pid_t)pid;
            break;
        }
    }
    pclose(fp);
    return result;
}

static pid_t resolve_app_pid(const char *app_name) {
    pid_t pid = resolve_with_pgrep(app_name);
    if (pid > 0) return pid;
    pid = resolve_with_ps(app_name);
    if (pid > 0) return pid;
    fprintf(stderr, "Application \"%s\" is not running.\n", app_name);
    return -1;
}

/* ── Usage / argument parsing ────────────────────────────────────── */

static void print_usage(void) {
    fprintf(stderr,
        "Usage: axtrace [options] <app-name>\n"
        "\n"
        "Options:\n"
        "  --depth <n>        Maximum traversal depth (default: %d)\n"
        "  --focus-only       Trace from the focused AX element\n"
        "  --show-values      Include value=\"...\" for nodes with AXValue\n"
        "  --show-geometry    Show position and size of each element\n"
        "  --show-actions     List available accessibility actions\n"
        "  --show-all-attrs   Dump all attribute names per element\n"
        "  --role <pattern>   Filter to nodes whose role contains pattern\n"
        "  --search <text>    Show only subtrees with matching values\n"
        "  --json             Output as JSON\n"
        "  --count            Show element type statistics\n"
        "  --list-apps        List running macOS applications\n",
        DEFAULT_DEPTH);
}

static int parse_options(int argc, char **argv, Options *opts) {
    memset(opts, 0, sizeof *opts);
    opts->depth = DEFAULT_DEPTH;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--depth") == 0) {
            if (++i >= argc) { print_usage(); return -1; }
            opts->depth = atoi(argv[i]);
            if (opts->depth <= 0) { print_usage(); return -1; }
        } else if (strcmp(argv[i], "--focus-only") == 0) {
            opts->focus_only = 1;
        } else if (strcmp(argv[i], "--show-values") == 0) {
            opts->show_values = 1;
        } else if (strcmp(argv[i], "--show-geometry") == 0) {
            opts->show_geometry = 1;
        } else if (strcmp(argv[i], "--show-actions") == 0) {
            opts->show_actions = 1;
        } else if (strcmp(argv[i], "--show-all-attrs") == 0) {
            opts->show_all_attrs = 1;
        } else if (strcmp(argv[i], "--role") == 0) {
            if (++i >= argc) { print_usage(); return -1; }
            opts->role_filter = argv[i];
        } else if (strcmp(argv[i], "--search") == 0) {
            if (++i >= argc) { print_usage(); return -1; }
            opts->search_text = argv[i];
        } else if (strcmp(argv[i], "--json") == 0) {
            opts->json = 1;
        } else if (strcmp(argv[i], "--count") == 0) {
            opts->count = 1;
        } else if (strcmp(argv[i], "--list-apps") == 0) {
            opts->list_apps = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return -1;
        } else if (strncmp(argv[i], "--", 2) == 0) {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage();
            return -1;
        } else {
            if (opts->app_name) {
                fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
                print_usage();
                return -1;
            }
            opts->app_name = argv[i];
        }
    }

    if (!opts->list_apps && !opts->app_name) {
        print_usage();
        return -1;
    }

    return 0;
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    Options opts;
    if (parse_options(argc, argv, &opts) != 0)
        return 1;

    if (opts.list_apps) {
        if (!AXIsProcessTrusted()) {
            fprintf(stderr,
                "Accessibility permission required.\n"
                "Enable it in:\n"
                "System Settings > Privacy & Security > Accessibility\n");
            return 1;
        }
        list_apps();
        return 0;
    }

    if (!AXIsProcessTrusted()) {
        fprintf(stderr,
            "Accessibility permission required.\n"
            "Enable it in:\n"
            "System Settings > Privacy & Security > Accessibility\n");
        return 1;
    }

    pid_t pid = resolve_app_pid(opts.app_name);
    if (pid < 0) return 1;

    AXUIElementRef app = AXUIElementCreateApplication(pid);

    AXUIElementRef root_el = app;
    AXUIElementRef focused = NULL;
    if (opts.focus_only) {
        CFStringRef focus_attr = cf_attr("AXFocusedUIElement");
        CFTypeRef val = ax_get_attr(app, focus_attr);
        CFRelease(focus_attr);
        if (!val || CFGetTypeID(val) != AXUIElementGetTypeID()) {
            fprintf(stderr, "No focused accessibility element found for \"%s\".\n", opts.app_name);
            CFRelease(app);
            if (val) CFRelease(val);
            return 1;
        }
        focused = (AXUIElementRef)val;
        root_el = focused;
    }

    Node *root = build_tree(root_el, &opts);
    if (!root) {
        fprintf(stderr, "Unable to inspect accessibility tree for \"%s\".\n", opts.app_name);
        if (focused) CFRelease(focused);
        CFRelease(app);
        return 1;
    }

    if (opts.search_text)
        mark_search(root, opts.search_text);

    if (opts.count)
        print_count(root);
    else if (opts.json)
        print_json(root, &opts);
    else
        print_tree(root, &opts);

    if (focused) CFRelease(focused);
    CFRelease(app);
    return 0;
}
