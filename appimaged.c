/*
 * 
 * Watch directories for AppImages and register/unregister them with the system
 * 
 * TODO:
 * - Also watch /tmp/.mount* and resolve to the launching AppImage
 * - Add and remove subdirectories on the fly - see https://github.com/paragone/configure-via-inotify/blob/master/inotify/src/inotifywatch.c
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <inotifytools/inotifytools.h>
#include <inotifytools/inotify.h>

#include <glib.h>
#include <glib/gprintf.h>

#include "shared.c"

#include <pthread.h>

static gboolean verbose = FALSE;
gchar **remaining_args = NULL;

static GOptionEntry entries[] =
{
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Be verbose", NULL },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &remaining_args, NULL },
    { NULL }
};

#define EXCLUDE_CHUNK 1024
#define WR_EVENTS (IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF)

int add_dir_to_watch(char *dir)
{
    if (g_file_test (dir, G_FILE_TEST_IS_DIR)){
        if(!inotifytools_watch_recursively(dir, WR_EVENTS) ) {
            fprintf(stderr, "%s\n", strerror(inotifytools_error()));
            exit(1);
        }
    }
}

/* Run the actual work in treads; 
 * pthread allows to pass only one argument to the thread function, 
 * hence we use a struct as the argument in which the real arguments are */
struct arg_struct {
    char* path;
    gboolean verbose;
};

void *thread_appimage_register_in_system(void *arguments)
{
    struct arg_struct *args = arguments;
    appimage_register_in_system(args->path, args->verbose);
    pthread_exit(NULL);
}

void *thread_appimage_unregister_in_system(void *arguments)
{
    struct arg_struct *args = arguments;
    appimage_unregister_in_system(args->path, args->verbose);
    pthread_exit(NULL);
}

void handle_event(struct inotify_event *event)
{
    int ret;
    gchar *absolute_path = g_build_path(G_DIR_SEPARATOR_S, inotifytools_filename_from_wd(event->wd), event->name, NULL);
    
    if(event->mask & IN_CLOSE_WRITE | event->mask & IN_MOVED_TO){
        if(g_file_test(absolute_path, G_FILE_TEST_IS_REGULAR)){
            pthread_t some_thread;
            struct arg_struct args;
            args.path = absolute_path;
            args.verbose = verbose;
            ret = pthread_create(&some_thread, NULL, thread_appimage_register_in_system, &args);
            if (!ret) {
                pthread_join(some_thread, NULL);
            }
        }
    }
    
    if(event->mask & IN_MOVED_FROM | event->mask & IN_DELETE){
        pthread_t some_thread;
        struct arg_struct args;
        args.path = absolute_path;
        args.verbose = verbose;
        ret = pthread_create(&some_thread, NULL, thread_appimage_unregister_in_system, &args);
        if (!ret) {
            pthread_join(some_thread, NULL);
        }
    }
    
    /* Too many FS events were received, some event notifications were potentially lost */
    if (event->mask & IN_Q_OVERFLOW){
        printf ("Warning: AN OVERFLOW EVENT OCCURRED\n");
    }
    
    if(event->mask & IN_IGNORED){
        printf ("Warning: AN IN_IGNORED EVENT OCCURRED\n");
    }
    
}

int main(int argc, char ** argv) {
    
    GError *error = NULL;
    GOptionContext *context;
    
    context = g_option_context_new ("");
    g_option_context_add_main_entries (context, entries, NULL);
    // g_option_context_add_group (context, gtk_get_option_group (TRUE));
    if (!g_option_context_parse (context, &argc, &argv, &error))
    {
        g_print("option parsing failed: %s\n", error->message);
        exit (1);
    }
    
    if ( !inotifytools_initialize()){
        fprintf(stderr, "inotifytools_initialize error\n");
        exit(1);
    }
    
    add_dir_to_watch(g_build_filename(g_get_home_dir(), "/Downloads", NULL));
    add_dir_to_watch(g_build_filename(g_get_home_dir(), "/bin", NULL));
    add_dir_to_watch(g_build_filename("/Applications", NULL));
    add_dir_to_watch(g_build_filename("/isodevice/Applications", NULL));
    add_dir_to_watch(g_build_filename("/opt", NULL));
    add_dir_to_watch(g_build_filename("/usr/local/bin", NULL));
    
    struct inotify_event * event = inotifytools_next_event(-1);
    while (event) {
        if(verbose){
            inotifytools_printf(event, "%w%f %e\n");
        }
        fflush(stdout);        
        handle_event(event);
        fflush(stdout);        
        event = inotifytools_next_event(-1);
    }
}
