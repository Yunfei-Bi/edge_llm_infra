#pragma once

#include <vector>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <iostream>

#include "pzmq.hpp"
#include "all.h"
#include "unit_data.h"

using namespace StackFlows;

void remote_server_work();
void remote_server_stop_work();