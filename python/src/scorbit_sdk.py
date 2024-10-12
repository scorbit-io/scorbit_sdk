class ScorbitSDK:
    # ... existing code ...

    @staticmethod
    def create_game_state():
        if ScorbitSDK._net_instance:
            ScorbitSDK._current_game = GameState(ScorbitSDK._net_instance)
            return ScorbitSDK._current_game
        else:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")

    @staticmethod
    def destroy_game_state():
        ScorbitSDK._current_game = None

    @staticmethod
    def set_game_started():
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.set_game_started()
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    def set_game_finished():
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.set_game_finished()
            ScorbitSDK._current_game.send_session_log()
            ScorbitSDK._current_game = None
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    def set_current_ball(ball: int):
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.set_current_ball(ball)
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    def set_active_player(player: int):
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.set_active_player(player)
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    def set_score(player: int, score: int):
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.set_score(player, score)
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    def add_mode(mode: str):
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.add_mode(mode)
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    def remove_mode(mode: str):
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.remove_mode(mode)
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    def clear_modes():
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.clear_modes()
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    async def commit():
        if ScorbitSDK._current_game:
            await ScorbitSDK._current_game.commit()
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    # Keep the update_game method for backwards compatibility
    @staticmethod
    async def update_game(active: bool, current_player: int=None, current_ball: int=None, 
                          player_scores: Dict[int, int]=None, game_modes: List[str]=None, callback=None):
        if ScorbitSDK._current_game:
            if current_player is not None:
                ScorbitSDK.set_active_player(current_player)
            if current_ball is not None:
                ScorbitSDK.set_current_ball(current_ball)
            if player_scores:
                for player, score in player_scores.items():
                    ScorbitSDK.set_score(player, score)
            if game_modes:
                ScorbitSDK.clear_modes()
                for mode in game_modes:
                    ScorbitSDK.add_mode(mode)
            await ScorbitSDK.commit()
            if callback:
                callback()
            if not active:
                ScorbitSDK.set_game_finished()
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

# Expose ScorbitSDK methods for easier access
create_game_state = ScorbitSDK.create_game_state
destroy_game_state = ScorbitSDK.destroy_game_state
set_game_started = ScorbitSDK.set_game_started
set_game_finished = ScorbitSDK.set_game_finished
set_current_ball = ScorbitSDK.set_current_ball
set_active_player = ScorbitSDK.set_active_player
set_score = ScorbitSDK.set_score
add_mode = ScorbitSDK.add_mode
remove_mode = ScorbitSDK.remove_mode
clear_modes = ScorbitSDK.clear_modes
commit = ScorbitSDK.commit
update_game = ScorbitSDK.update_game