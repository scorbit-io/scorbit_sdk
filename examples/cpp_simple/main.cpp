/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/net_types.h>
#include <scorbit_sdk/scorbit_sdk.h>
#include <iostream>

using namespace std;

void gameCycle(scorbit::GameState &gs)
{
    (void)gs;
}

int main()
{
    scorbit::SignerCallback signer;
    scorbit::GameState gs  = scorbit::createGameState(std::move(signer));
    cout << "Hello World!" << endl;
    return 0;
}
