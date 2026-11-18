#ifndef SOURCE_MTRANDOMNAMEGENERATOR_H_
#define SOURCE_MTRANDOMNAMEGENERATOR_H_

#include "mtStructs.h"

constexpr uint32_t RANDOM_NAME_TABS_SIZE = 1006;
constexpr uint8_t RANDOM_NAME_WORD_1_MAX_SIZE = 30;
constexpr uint8_t RANDOM_NAME_WORD_2_MAX_SIZE = 30;
constexpr uint8_t RANDOM_NAME_MAX_SIZE =
  RANDOM_NAME_WORD_1_MAX_SIZE + RANDOM_NAME_WORD_2_MAX_SIZE + 1;

class Timer;

class mtRandomNameGenerator
{
  public:
    const char* getRandomName(Timer* timer);

  private:
    char randomName[RANDOM_NAME_MAX_SIZE];
    bool tabsReady;   // the tables are inflated (first call). No initialiser: the global is zeroed, an initialiser would add a static constructor
};

extern mtRandomNameGenerator randomNameGenerator;

#endif /* SOURCE_MTRANDOMNAMEGENERATOR_H_ */
