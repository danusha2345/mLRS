//*******************************************************
// Parser execution budget
//*******************************************************
#ifndef PARSER_BUDGET_H
#define PARSER_BUDGET_H
#pragma once


#include <stdint.h>


#define SERIAL_PARSER_BYTE_BUDGET  64


class tParserByteBudget
{
  public:
    tParserByteBudget(void) : remaining(SERIAL_PARSER_BYTE_BUDGET) {}

    bool Take(void)
    {
        if (!remaining) return false;
        remaining--;
        return true;
    }

  private:
    uint8_t remaining;
};


#endif // PARSER_BUDGET_H
