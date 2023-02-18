#include "util.h"
#include <stdio.h>

void write_to_file(struct options *opts, const char * server_name, const char * function_name, double time_taken) {

    fprintf(opts->csv_file, "%s,%s,%f\n", server_name, function_name, time_taken); // NOLINT(cert-err33-c)

    printf("%s took %f ms to execute \n", function_name, time_taken);
    // Flush the stream
    fclose(opts->csv_file); // NOLINT(cert-err33-c)
    opts->csv_file = fopen("states.csv", "ae");
}
