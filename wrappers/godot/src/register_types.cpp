/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "scorbit_game_state.h"
#include "sdk_loader.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

namespace {

void initializeModule(ModuleInitializationLevel level)
{
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(scorbit_godot::ScorbitGameState);
}

void uninitializeModule(ModuleInitializationLevel level)
{
    if (level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        if (scorbit_godot::sdkLoaded()) {
            // The SDK must not call back into this library once it is unloaded.
            scorbit_godot::api().sb_reset_logger();
        }
    }
}

} // namespace

extern "C" {

// Named by entry_symbol in addons/scorbit/scorbit.gdextension.
GDExtensionBool GDE_EXPORT scorbit_godot_init(GDExtensionInterfaceGetProcAddress getProcAddress,
                                              GDExtensionClassLibraryPtr library,
                                              GDExtensionInitialization *initialization)
{
    GDExtensionBinding::InitObject init(getProcAddress, library, initialization);
    init.register_initializer(initializeModule);
    init.register_terminator(uninitializeModule);
    init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init.init();
}
}
