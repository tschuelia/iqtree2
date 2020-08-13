//
//  operatingsystem.h
//  iqtree
//
//  Created by James Barbetti on 29/7/20.
//

#ifndef operatingsystem_h
#define operatingsystem_h

#include <string>

#if defined(CLANG_UNDER_VS)
#define CONSOLE_FILE "CON:"
#else
#define CONSOLE_FILE "/dev/tty"
#endif

std::string getOSName();
bool isStandardOutputATerminal();

#endif /* operatingsystem_h */