#ifndef MODULE_H
#define MODULE_H

#define __STDC_FORMAT_MACROS
#include <stdint.h>

/**
 * Common class for the parameter generation tool.
 * All parameter generation classes extend this one.\n
 * Contain some common error messages.\n
 * Contain virtual classes that needs to be implemented
 */
class ParamModule {

  protected:
    /* common error messages */
    //! Error message: Unknown option
    static const char * const unknownOption;
    //! Error message: Too much argument were passed
    static const char * const tooMuchOption;

  public:

    /**
     * Module initialisation.
     * Parses the arguments
     * @param argc Argument Count
     * @param argv Argument Vector
     * @param human Generate output human-readable or not
     * @param error String to be passed back in case of error
     * @return Ok: 0, anything else in case of error (error code)
     */
    virtual int init(const int argc, char **argv, const bool human, const char** error) = 0;

    /**
     * Clean the module
     */
    virtual void clean() = 0;

    /**
     * Add input char
     * @param in True if the packet was received, False if it wasn't
     * @return Ok: 0, anything else in case of error (error code)
     */
    virtual int addChar(const bool in) = 0;

    /**
      * Is-there a 2nd round ?
      * (prepare the module to the potential 2nd round
      * @return True if there is a 2nd round, false if not
      */
    virtual bool nextRound() = 0;

    /**
     * Finalise the data.
     * @param max_rand CLICK_RAND_MAX used by click
     */
    virtual void finalize(const uint32_t max_rand) = 0;

    /**
     * Print a binary output
     */
    virtual void printBinary() = 0;

    /**
     * Print a human-readable output
     * @param max_rand CLICK_RAND_MAX used by click
     */
    virtual void printHuman(const uint32_t max_rand) = 0;
};

#endif
