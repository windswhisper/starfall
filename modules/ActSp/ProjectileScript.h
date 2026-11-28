#pragma once
#include <GeneralStructures.h>
class BulletClass;
namespace ProjectileScript {
void reset();
void forget(DWORD id);
bool launch(BulletClass* bullet);
DWORD move(BulletClass* bullet);
}
