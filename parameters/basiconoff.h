#ifndef BASICONOFF_H
#define BASICONOFF_H

#include "module.h"
#include <getopt.h>
#include <map>
#include <iostream>
#include <fstream>

class ParamBasicOnOff : public ParamModule {

  private:

    /* static stuff */
    static const struct option basiconoff_long_options[];
    static const char * const needfiles;

    /* Variables */
    uint64_t success_total;
    uint64_t error_total;

    std::map<uint32_t, uint64_t> success_length;
    std::map<uint32_t, uint64_t> error_length;

    std::map<uint32_t, uint32_t> success_length_final;
    std::map<uint32_t, uint32_t> error_length_final;

    bool current_state;
    uint64_t length;

    /* Output */
    const char *error_filename, *free_filename;

    /* Error String */
    static const char * const knotset;

    /* functions */
    static void calculate_values(const uint32_t, const std::map<uint32_t, uint64_t>&, const uint64_t, std::map<uint32_t, uint32_t>&);
    static void printBinaryToFile(const std::map<uint32_t, uint32_t>&, const char*);
    static void printHumanToStream(const uint32_t, const std::map<uint32_t, uint32_t>&, std::ostream&);

  public:

    /* Initialize the module */
    int init(const int, char **, const bool, const char**);

    /* Clean the module */
    void clean();

    /* Add input char */
    int addChar(const bool);

    /* Is-there a 2nd round ? (prepare the module to the potential 2nd round */
    bool nextRound();

    /* Finalise the data */
    void finalize(const uint32_t);

    /* Output */
    void printBinary();
    void printHuman(const uint32_t);
};

#endif
