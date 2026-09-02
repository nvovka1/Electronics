#pragma once

// Owns the position state and sequences one move per command:
// relay on -> driver enabled -> steps -> driver disabled -> relay off.
void motorTask(void* parameter);
