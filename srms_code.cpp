#include <gtk/gtk.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define STUDENT_FILE "students.txt"
#define CREDENTIAL_FILE "credentials.txt"

/* Columns for treeview */
enum {
    COL_REGNO,
    COL_NAME,
    COL_YEAR,
    COL_SEM,
    COL_CGPA1,
    COL_CGPA2,
    COL_CGPA3,
    COL_CGPA4,
    N_COLUMNS
};

/* Current user and role */
static char current_user[64] = "";
static char current_role[32] = "";

/* Structs */
typedef struct {
    GtkListStore *store;
    GtkTreeView *tree;
    GtkWindow *parent;
} AppData;

struct Student {
    char reg_no[32];
    char name[128];
    int year;
    int semester;
    double cgpa[4]; // cgpa[0] = year1, cgpa[1] = year2 ...
};

/* Forward declarations */
static void show_login_dialog(GtkWindow *parent);
static void show_main_window(GtkWindow *parent);
static void show_add_student_dialog(GtkWindow *parent, GtkListStore *store);
static void show_update_student_dialog(GtkWindow *parent, GtkListStore *store, Student student, GtkTreeIter iter);
static void show_search_by_regno_dialog(GtkWindow *parent, GtkListStore *store);
static void refresh_tree_store(GtkListStore *store);
static void save_store_to_file(GtkListStore *store);
static gboolean load_credentials(const char *username, const char *password, char *out_role, size_t role_len);
static void show_message(GtkWindow *parent, const char *title, const char *message);
static void delete_selected_student(GtkWindow *parent, GtkTreeView *treeview);

/* Helpers */
static void ensure_default_credentials_and_files() {
    /* credentials */
    FILE *cf = fopen(CREDENTIAL_FILE, "r");
    if (!cf) {
        cf = fopen(CREDENTIAL_FILE, "w");
        if (cf) {
            fprintf(cf, "admin admin123 ADMIN\n");
            fprintf(cf, "staff staff123 STAFF\n");
            fprintf(cf, "student 123456 USER\n");
            fprintf(cf, "guest guest GUEST\n");
            fclose(cf);
        }
    } else fclose(cf);

    /* students file - create if missing */
    FILE *sf = fopen(STUDENT_FILE, "a");
    if (sf) fclose(sf);
}

/* Read credentials file and match */
static gboolean load_credentials(const char *username, const char *password, char *out_role, size_t role_len) {
    FILE *fp = fopen(CREDENTIAL_FILE, "r");
    if (!fp) return FALSE;
    char u[128], p[128], r[64];
    while (fscanf(fp, "%127s %127s %63s", u, p, r) == 3) {
        if (strcmp(u, username) == 0 && strcmp(p, password) == 0) {
            strncpy(out_role, r, role_len - 1);
            out_role[role_len - 1] = '\0';
            fclose(fp);
            return TRUE;
        }
    }
    fclose(fp);
    return FALSE;
}

/* Show a simple message dialog */
static void show_message(GtkWindow *parent, const char *title, const char *message) {
    GtkWidget *dlg = gtk_message_dialog_new(parent,
        (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
        "%s", message);
    gtk_window_set_title(GTK_WINDOW(dlg), title);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

/* Load students.txt into liststore */
static void refresh_tree_store(GtkListStore *store) {
    gtk_list_store_clear(store);
    FILE *fp = fopen(STUDENT_FILE, "r");
    if (!fp) return;
    char reg[32], name[128];
    int year = 0, sem = 0;
    double cg1 = 0.0, cg2 = 0.0, cg3 = 0.0, cg4 = 0.0;
    while (fscanf(fp, "%31s %127s %d %d %lf %lf %lf %lf",
                  reg, name, &year, &sem, &cg1, &cg2, &cg3, &cg4) >= 4) {
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           COL_REGNO, reg,
                           COL_NAME, name,
                           COL_YEAR, year,
                           COL_SEM, sem,
                           COL_CGPA1, cg1,
                           COL_CGPA2, cg2,
                           COL_CGPA3, cg3,
                           COL_CGPA4, cg4,
                           -1);
    }
    fclose(fp);
}

/* Save liststore contents to students.txt */
static void save_store_to_file(GtkListStore *store) {
    FILE *fp = fopen(STUDENT_FILE, "w");
    if (!fp) return;
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    while (valid) {
        char *regno = nullptr;
        char *name = nullptr;
        int year = 0, sem = 0;
        double cg1 = 0.0, cg2 = 0.0, cg3 = 0.0, cg4 = 0.0;
        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
                           COL_REGNO, &regno,
                           COL_NAME, &name,
                           COL_YEAR, &year,
                           COL_SEM, &sem,
                           COL_CGPA1, &cg1,
                           COL_CGPA2, &cg2,
                           COL_CGPA3, &cg3,
                           COL_CGPA4, &cg4,
                           -1);
        /* regno and name are freshly allocated by gtk_tree_model_get for string columns */
        if (regno && name) {
            fprintf(fp, "%s %s %d %d %.2f %.2f %.2f %.2f\n", regno, name, year, sem, cg1, cg2, cg3, cg4);
        }
        if (regno) g_free(regno);
        if (name) g_free(name);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }
    fclose(fp);
}

/* Add Student dialog */
static void show_add_student_dialog(GtkWindow *parent, GtkListStore *store) {
    /* Only ADMIN and STAFF allowed (should check before calling, but double-check here) */
    if (!(strcmp(current_role, "ADMIN") == 0 || strcmp(current_role, "STAFF") == 0)) {
        show_message(parent, "Permission denied", "Only admin and staff can add students.");
        return;
    }

    GtkWidget *dlg = gtk_dialog_new_with_buttons("Add Student", parent,
        (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Add", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);

    GtkWidget *reg_label = gtk_label_new("Registration No:");
    GtkWidget *name_label = gtk_label_new("Name:");
    GtkWidget *year_label = gtk_label_new("Current Year (1-4):");
    GtkWidget *sem_label = gtk_label_new("Current Sem (1-8):");
    GtkWidget *cg1_label = gtk_label_new("CGPA Year1:");
    GtkWidget *cg2_label = gtk_label_new("CGPA Year2:");
    GtkWidget *cg3_label = gtk_label_new("CGPA Year3:");
    GtkWidget *cg4_label = gtk_label_new("CGPA Year4:");

    GtkWidget *reg_entry = gtk_entry_new();
    GtkWidget *name_entry = gtk_entry_new();
    GtkWidget *year_entry = gtk_entry_new();
    GtkWidget *sem_entry = gtk_entry_new();
    GtkWidget *cg1_entry = gtk_entry_new();
    GtkWidget *cg2_entry = gtk_entry_new();
    GtkWidget *cg3_entry = gtk_entry_new();
    GtkWidget *cg4_entry = gtk_entry_new();

    gtk_grid_attach(GTK_GRID(grid), reg_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), reg_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), year_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), year_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sem_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sem_entry, 1, 3, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), cg1_label, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cg1_entry, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cg2_label, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cg2_entry, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cg3_label, 0, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cg3_entry, 1, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cg4_label, 0, 7, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cg4_entry, 1, 7, 1, 1);

    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dlg);

    gint res = gtk_dialog_run(GTK_DIALOG(dlg));
    if (res == GTK_RESPONSE_OK) {
        const char *reg = gtk_entry_get_text(GTK_ENTRY(reg_entry));
        const char *name = gtk_entry_get_text(GTK_ENTRY(name_entry));
        const char *syear = gtk_entry_get_text(GTK_ENTRY(year_entry));
        const char *ssem = gtk_entry_get_text(GTK_ENTRY(sem_entry));
        const char *scg1 = gtk_entry_get_text(GTK_ENTRY(cg1_entry));
        const char *scg2 = gtk_entry_get_text(GTK_ENTRY(cg2_entry));
        const char *scg3 = gtk_entry_get_text(GTK_ENTRY(cg3_entry));
        const char *scg4 = gtk_entry_get_text(GTK_ENTRY(cg4_entry));

        if (strlen(reg) == 0 || strlen(name) == 0 || strlen(syear) == 0 || strlen(ssem) == 0) {
            show_message(parent, "Error", "Registration number, name, year and semester are required.");
        } else {
            int year = atoi(syear);
            int sem = atoi(ssem);
            double cg1 = (strlen(scg1) ? atof(scg1) : 0.0);
            double cg2 = (strlen(scg2) ? atof(scg2) : 0.0);
            double cg3 = (strlen(scg3) ? atof(scg3) : 0.0);
            double cg4 = (strlen(scg4) ? atof(scg4) : 0.0);
            /* Append to file */
            FILE *fp = fopen(STUDENT_FILE, "a");
            if (!fp) {
                show_message(parent, "Error", "Cannot open students file for writing.");
            } else {
                fprintf(fp, "%s %s %d %d %.2f %.2f %.2f %.2f\n", reg, name, year, sem, cg1, cg2, cg3, cg4);
                fclose(fp);
                /* Add to store */
                GtkTreeIter iter;
                gtk_list_store_append(store, &iter);
                gtk_list_store_set(store, &iter,
                                   COL_REGNO, reg,
                                   COL_NAME, name,
                                   COL_YEAR, year,
                                   COL_SEM, sem,
                                   COL_CGPA1, cg1,
                                   COL_CGPA2, cg2,
                                   COL_CGPA3, cg3,
                                   COL_CGPA4, cg4,
                                   -1);
                show_message(parent, "Success", "Student added.");
            }
        }
    }

    gtk_widget_destroy(dlg);
}

/* Update dialog - using local copy of student data and iter (safe) */
static void show_update_student_dialog(GtkWindow *parent, GtkListStore *store, Student student, GtkTreeIter iter) {
    /* Only ADMIN and STAFF allowed */
    if (!(strcmp(current_role, "ADMIN") == 0 || strcmp(current_role, "STAFF") == 0)) {
        show_message(parent, "Permission denied", "Only admin and staff can update students.");
        return;
    }

    GtkWidget *dlg = gtk_dialog_new_with_buttons("Update Student", parent,
        (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Update", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);

    GtkWidget *reg_label = gtk_label_new("Registration No:");
    GtkWidget *name_label = gtk_label_new("Name:");
    GtkWidget *year_label = gtk_label_new("Current Year:");
    GtkWidget *sem_label = gtk_label_new("Current Sem:");
    GtkWidget *cg1_label = gtk_label_new("CGPA Year1:");
    GtkWidget *cg2_label = gtk_label_new("CGPA Year2:");
    GtkWidget *cg3_label = gtk_label_new("CGPA Year3:");
    GtkWidget *cg4_label = gtk_label_new("CGPA Year4:");

    GtkWidget *reg_entry = gtk_entry_new();
    GtkWidget *name_entry = gtk_entry_new();
    GtkWidget *year_entry = gtk_entry_new();
    GtkWidget *sem_entry = gtk_entry_new();
    GtkWidget *cg1_entry = gtk_entry_new();
    GtkWidget *cg2_entry = gtk_entry_new();
    GtkWidget *cg3_entry = gtk_entry_new();
    GtkWidget *cg4_entry = gtk_entry_new();

    gtk_entry_set_text(GTK_ENTRY(reg_entry), student.reg_no);
    gtk_widget_set_sensitive(reg_entry, FALSE); /* reg no shouldn't be changed */
    gtk_entry_set_text(GTK_ENTRY(name_entry), student.name);

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", student.year);
    gtk_entry_set_text(GTK_ENTRY(year_entry), buf);
    snprintf(buf, sizeof(buf), "%d", student.semester);
    gtk_entry_set_text(GTK_ENTRY(sem_entry), buf);
    snprintf(buf, sizeof(buf), "%.2f", student.cgpa[0]); gtk_entry_set_text(GTK_ENTRY(cg1_entry), buf);
    snprintf(buf, sizeof(buf), "%.2f", student.cgpa[1]); gtk_entry_set_text(GTK_ENTRY(cg2_entry), buf);
    snprintf(buf, sizeof(buf), "%.2f", student.cgpa[2]); gtk_entry_set_text(GTK_ENTRY(cg3_entry), buf);
    snprintf(buf, sizeof(buf), "%.2f", student.cgpa[3]); gtk_entry_set_text(GTK_ENTRY(cg4_entry), buf);

    gtk_grid_attach(GTK_GRID(grid), reg_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), reg_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), year_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), year_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sem_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), sem_entry, 1, 3, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), cg1_label, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cg1_entry, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cg2_label, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cg2_entry, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cg3_label, 0, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cg3_entry, 1, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cg4_label, 0, 7, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cg4_entry, 1, 7, 1, 1);

    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dlg);

    gint res = gtk_dialog_run(GTK_DIALOG(dlg));
    if (res == GTK_RESPONSE_OK) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(name_entry));
        const char *syear = gtk_entry_get_text(GTK_ENTRY(year_entry));
        const char *ssem = gtk_entry_get_text(GTK_ENTRY(sem_entry));
        const char *scg1 = gtk_entry_get_text(GTK_ENTRY(cg1_entry));
        const char *scg2 = gtk_entry_get_text(GTK_ENTRY(cg2_entry));
        const char *scg3 = gtk_entry_get_text(GTK_ENTRY(cg3_entry));
        const char *scg4 = gtk_entry_get_text(GTK_ENTRY(cg4_entry));

        if (strlen(name) == 0 || strlen(syear) == 0 || strlen(ssem) == 0) {
            show_message(parent, "Error", "Name, year and semester are required.");
        } else {
            int year = atoi(syear), sem = atoi(ssem);
            double cg1 = (strlen(scg1) ? atof(scg1) : 0.0);
            double cg2 = (strlen(scg2) ? atof(scg2) : 0.0);
            double cg3 = (strlen(scg3) ? atof(scg3) : 0.0);
            double cg4 = (strlen(scg4) ? atof(scg4) : 0.0);

            /* update the store using iter (safe because iter is from selection immediately before) */
            gtk_list_store_set(store, &iter,
                               COL_NAME, name,
                               COL_YEAR, year,
                               COL_SEM, sem,
                               COL_CGPA1, cg1,
                               COL_CGPA2, cg2,
                               COL_CGPA3, cg3,
                               COL_CGPA4, cg4,
                               -1);
            save_store_to_file(store);
            show_message(parent, "Updated", "Record updated.");
        }
    }

    gtk_widget_destroy(dlg);
}

/* Delete selected student */
static void delete_selected_student(GtkWindow *parent, GtkTreeView *treeview) {
    /* Only ADMIN and STAFF allowed */
    if (!(strcmp(current_role, "ADMIN") == 0 || strcmp(current_role, "STAFF") == 0)) {
        show_message(parent, "Permission denied", "Only admin and staff can delete students.");
        return;
    }

    GtkTreeSelection *sel = gtk_tree_view_get_selection(treeview);
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) {
        show_message(parent, "No selection", "Please select a student first.");
        return;
    }

    char *regno = nullptr;
    char *name = nullptr;
    int year = 0, sem = 0;
    double cg1 = 0.0, cg2 = 0.0, cg3 = 0.0, cg4 = 0.0;
    gtk_tree_model_get(model, &iter,
                       COL_REGNO, &regno,
                       COL_NAME, &name,
                       COL_YEAR, &year,
                       COL_SEM, &sem,
                       COL_CGPA1, &cg1,
                       COL_CGPA2, &cg2,
                       COL_CGPA3, &cg3,
                       COL_CGPA4, &cg4,
                       -1);

    char buf[512];
    snprintf(buf, sizeof(buf), "Delete this record?\n\nReg No: %s\nName: %s\nYear: %d  Sem: %d\nCGPAs: %.2f %.2f %.2f %.2f",
             regno ? regno : "", name ? name : "", year, sem, cg1, cg2, cg3, cg4);

    if (regno) g_free(regno);
    if (name) g_free(name);

    GtkWidget *conf = gtk_message_dialog_new(parent,
        (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
        "%s", buf);
    gint res = gtk_dialog_run(GTK_DIALOG(conf));
    gtk_widget_destroy(conf);

    if (res == GTK_RESPONSE_YES) {
        GtkListStore *store = GTK_LIST_STORE(model);
        gtk_list_store_remove(store, &iter);
        save_store_to_file(store);
        show_message(parent, "Deleted", "Record deleted.");
    }
}

/* Search a student by registration number (for students to view their own details) */
static void show_search_by_regno_dialog(GtkWindow *parent, GtkListStore *store) {
    GtkWidget *dlg = gtk_dialog_new_with_buttons("Find Student (by Reg No)", parent,
        (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Find", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *label = gtk_label_new("Registration No:");
    GtkWidget *entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(content), hbox);
    gtk_widget_show_all(dlg);

    gint res = gtk_dialog_run(GTK_DIALOG(dlg));
    if (res == GTK_RESPONSE_OK) {
        const char *reg = gtk_entry_get_text(GTK_ENTRY(entry));
        if (strlen(reg) == 0) {
            show_message(parent, "Error", "Please enter registration number.");
        } else {
            GtkTreeIter iter;
            gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
            gboolean found = FALSE;
            while (valid) {
                char *regno = nullptr;
                gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, COL_REGNO, &regno, -1);
                if (regno) {
                    if (strcmp(regno, reg) == 0) {
                        g_free(regno);
                        found = TRUE;
                        break;
                    }
                    g_free(regno);
                }
                valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
            }
            if (found) {
                char *regno = nullptr;
                char *name = nullptr;
                int year = 0, sem = 0;
                double cg1 = 0.0, cg2 = 0.0, cg3 = 0.0, cg4 = 0.0;
                gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
                                   COL_REGNO, &regno,
                                   COL_NAME, &name,
                                   COL_YEAR, &year,
                                   COL_SEM, &sem,
                                   COL_CGPA1, &cg1,
                                   COL_CGPA2, &cg2,
                                   COL_CGPA3, &cg3,
                                   COL_CGPA4, &cg4,
                                   -1);
                char info[512];
                snprintf(info, sizeof(info),
                         "Reg No: %s\nName: %s\nYear: %d\nSem: %d\nCGPA Yr1: %.2f\nCGPA Yr2: %.2f\nCGPA Yr3: %.2f\nCGPA Yr4: %.2f",
                         regno ? regno : "", name ? name : "", year, sem, cg1, cg2, cg3, cg4);
                if (regno) g_free(regno);
                if (name) g_free(name);
                show_message(parent, "Student Details", info);
            } else {
                show_message(parent, "Not found", "No student with that registration number.");
            }
        }
    }
    gtk_widget_destroy(dlg);
}

/* Main window and callbacks */
static void add_btn_cb(GtkButton *b, gpointer user_data) {
    AppData *d = (AppData *)user_data;
    show_add_student_dialog(d->parent, d->store);
}
static void refresh_btn_cb(GtkButton *b, gpointer user_data) {
    GtkListStore *store = (GtkListStore *)user_data;
    refresh_tree_store(store);
}
static void update_btn_cb(GtkButton *b, gpointer user_data) {
    AppData *d = (AppData *)user_data;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(d->tree);
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) {
        show_message(d->parent, "No selection", "Please select a student to update.");
        return;
    }
    /* copy student data locally */
    char *regno = nullptr; char *name = nullptr;
    int year = 0, sem = 0;
    double cg1 = 0.0, cg2 = 0.0, cg3 = 0.0, cg4 = 0.0;
    gtk_tree_model_get(model, &iter,
                       COL_REGNO, &regno,
                       COL_NAME, &name,
                       COL_YEAR, &year,
                       COL_SEM, &sem,
                       COL_CGPA1, &cg1,
                       COL_CGPA2, &cg2,
                       COL_CGPA3, &cg3,
                       COL_CGPA4, &cg4,
                       -1);
    Student s;
    if (regno) { strncpy(s.reg_no, regno, sizeof(s.reg_no)-1); s.reg_no[sizeof(s.reg_no)-1] = '\0'; g_free(regno); } else s.reg_no[0] = '\0';
    if (name) { strncpy(s.name, name, sizeof(s.name)-1); s.name[sizeof(s.name)-1] = '\0'; g_free(name); } else s.name[0] = '\0';
    s.year = year; s.semester = sem; s.cgpa[0] = cg1; s.cgpa[1] = cg2; s.cgpa[2] = cg3; s.cgpa[3] = cg4;
    show_update_student_dialog(d->parent, d->store, s, iter);
}
static void delete_btn_cb(GtkButton *b, gpointer user_data) {
    AppData *d = (AppData *)user_data;
    delete_selected_student(d->parent, d->tree);
}
static void search_btn_cb(GtkButton *b, gpointer user_data) {
    AppData *d = (AppData *)user_data;
    /* STUDENT role should use this to view own details; ADMIN/STAFF/GUEST can also use */
    if (strcmp(current_role, "USER") == 0) {
        /* Student - allow only self view by reg no */
        show_search_by_regno_dialog(d->parent, d->store);
    } else if (strcmp(current_role, "GUEST") == 0) {
        /* Guests can search and view */
        show_search_by_regno_dialog(d->parent, d->store);
    } else {
        /* Admin/Staff can also search */
        show_search_by_regno_dialog(d->parent, d->store);
    }
}
static void logout_btn_cb(GtkButton *b, gpointer user_data) {
    GtkWindow *w = GTK_WINDOW(user_data);
    gtk_widget_destroy(GTK_WIDGET(w));
    current_user[0] = '\0'; current_role[0] = '\0';
    show_login_dialog(nullptr);
}
static void row_activated_cb(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *col, gpointer userdata) {
    AppData *d = (AppData *)userdata;
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    if (gtk_tree_model_get_iter(model, &iter, path)) {
        /* show update dialog for admin/staff; for guest/student, show details dialog */
        char *regno = nullptr; char *name = nullptr;
        int year = 0, sem = 0;
        double cg1 = 0.0, cg2 = 0.0, cg3 = 0.0, cg4 = 0.0;
        gtk_tree_model_get(model, &iter,
                           COL_REGNO, &regno,
                           COL_NAME, &name,
                           COL_YEAR, &year,
                           COL_SEM, &sem,
                           COL_CGPA1, &cg1,
                           COL_CGPA2, &cg2,
                           COL_CGPA3, &cg3,
                           COL_CGPA4, &cg4,
                           -1);
        if (regno && name) {
            Student s;
            strncpy(s.reg_no, regno, sizeof(s.reg_no)-1); s.reg_no[sizeof(s.reg_no)-1] = '\0';
            strncpy(s.name, name, sizeof(s.name)-1); s.name[sizeof(s.name)-1] = '\0';
            s.year = year; s.semester = sem; s.cgpa[0]=cg1; s.cgpa[1]=cg2; s.cgpa[2]=cg3; s.cgpa[3]=cg4;
            g_free(regno); g_free(name);
            if (strcmp(current_role, "ADMIN") == 0 || strcmp(current_role, "STAFF") == 0) {
                show_update_student_dialog(d->parent, d->store, s, iter);
            } else {
                /* show read-only message */
                char info[512];
                snprintf(info, sizeof(info),
                         "Reg No: %s\nName: %s\nYear: %d\nSem: %d\nCGPA Yr1: %.2f\nCGPA Yr2: %.2f\nCGPA Yr3: %.2f\nCGPA Yr4: %.2f",
                         s.reg_no, s.name, s.year, s.semester, s.cgpa[0], s.cgpa[1], s.cgpa[2], s.cgpa[3]);
                show_message(d->parent, "Student Details", info);
            }
        }
    }
}

/* Build main window */
static void show_main_window(GtkWindow *parent) {
    /* ensure files exist (credentials/student) */
    ensure_default_credentials_and_files();

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    char title[128];
    snprintf(title, sizeof(title), "SRMS - User: %s (Role: %s)", current_user[0] ? current_user : "Unknown", current_role[0] ? current_role : "NONE");
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 500);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* List store and tree */
    GtkListStore *store = gtk_list_store_new(N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
    refresh_tree_store(store);

    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    GtkCellRenderer *r;

    r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c1 = gtk_tree_view_column_new_with_attributes("Reg No", r, "text", COL_REGNO, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c1);

    r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c2 = gtk_tree_view_column_new_with_attributes("Name", r, "text", COL_NAME, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c2);

    r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c3 = gtk_tree_view_column_new_with_attributes("Year", r, "text", COL_YEAR, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c3);

    r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c4 = gtk_tree_view_column_new_with_attributes("Sem", r, "text", COL_SEM, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c4);

    r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c5 = gtk_tree_view_column_new_with_attributes("CGPA Y1", r, "text", COL_CGPA1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c5);

    r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c6 = gtk_tree_view_column_new_with_attributes("CGPA Y2", r, "text", COL_CGPA2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c6);

    r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c7 = gtk_tree_view_column_new_with_attributes("CGPA Y3", r, "text", COL_CGPA3, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c7);

    r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c8 = gtk_tree_view_column_new_with_attributes("CGPA Y4", r, "text", COL_CGPA4, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c8);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), tree);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

    /* buttons */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    GtkWidget *add_btn = gtk_button_new_with_label("Add");
    GtkWidget *update_btn = gtk_button_new_with_label("Update");
    GtkWidget *delete_btn = gtk_button_new_with_label("Delete");
    GtkWidget *refresh_btn = gtk_button_new_with_label("Refresh");
    GtkWidget *search_btn = gtk_button_new_with_label("Find / View");
    GtkWidget *logout_btn = gtk_button_new_with_label("Logout");

    gtk_box_pack_start(GTK_BOX(hbox), add_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), update_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), delete_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), refresh_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), search_btn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), logout_btn, FALSE, FALSE, 0);

    /* Set permissions based on role */
    bool can_modify = (strcmp(current_role, "ADMIN") == 0 || strcmp(current_role, "STAFF") == 0);
    bool is_student = (strcmp(current_role, "USER") == 0);
    bool is_guest = (strcmp(current_role, "GUEST") == 0);

    gtk_widget_set_sensitive(add_btn, can_modify);
    gtk_widget_set_sensitive(update_btn, can_modify);
    gtk_widget_set_sensitive(delete_btn, can_modify);
    /* Guests and students cannot modify; guests can view; students can only view self via Find / View */

    /* AppData */
    AppData *ad = (AppData *)g_malloc(sizeof(AppData));
    ad->store = store; ad->tree = GTK_TREE_VIEW(tree); ad->parent = GTK_WINDOW(window);

    g_signal_connect(add_btn, "clicked", G_CALLBACK(add_btn_cb), ad);
    g_signal_connect(update_btn, "clicked", G_CALLBACK(update_btn_cb), ad);
    g_signal_connect(delete_btn, "clicked", G_CALLBACK(delete_btn_cb), ad);
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(refresh_btn_cb), store);
    g_signal_connect(search_btn, "clicked", G_CALLBACK(search_btn_cb), ad);
    g_signal_connect(logout_btn, "clicked", G_CALLBACK(logout_btn_cb), window);
    g_signal_connect(tree, "row-activated", G_CALLBACK(row_activated_cb), ad);

    /* free appdata when window destroyed */
    g_signal_connect(window, "destroy", G_CALLBACK(g_free), ad);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);
}

/* Login dialog */
static void show_login_dialog(GtkWindow *parent) {
    ensure_default_credentials_and_files();

    GtkWidget *dlg = gtk_dialog_new_with_buttons("Login", parent,
        (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Login", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);

    GtkWidget *user_label = gtk_label_new("Username:");
    GtkWidget *pass_label = gtk_label_new("Password:");
    GtkWidget *user_entry = gtk_entry_new();
    GtkWidget *pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(pass_entry), FALSE);

    gtk_grid_attach(GTK_GRID(grid), user_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), user_entry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), pass_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), pass_entry, 1, 1, 1, 1);

    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dlg);

    gint res = gtk_dialog_run(GTK_DIALOG(dlg));
    if (res == GTK_RESPONSE_OK) {
        const char *username = gtk_entry_get_text(GTK_ENTRY(user_entry));
        const char *password = gtk_entry_get_text(GTK_ENTRY(pass_entry));
        char rolebuf[64] = "";
        if (load_credentials(username, password, rolebuf, sizeof(rolebuf))) {
            strncpy(current_user, username, sizeof(current_user)-1);
            strncpy(current_role, rolebuf, sizeof(current_role)-1);
            gtk_widget_destroy(dlg);
            show_main_window(nullptr);
            return;
        } else {
            show_message(parent, "Login Failed", "Invalid username or password.");
        }
    }
    gtk_widget_destroy(dlg);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    show_login_dialog(nullptr);
    gtk_main();
    return 0;
}
