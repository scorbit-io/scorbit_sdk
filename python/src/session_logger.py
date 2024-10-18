import csv
import os
from datetime import datetime
from .net import Net

class SessionLogger:
    def __init__(self, uuid):
        self.uuid = uuid
        self.records = []
        self.log_file_path = f"{uuid}.csv"

    def log_event(self, current_scores, current_player, current_ball, game_modes):
        record = {
            'session_time': int(datetime.now().timestamp() * 1000),  # in milliseconds
            'current_p1_score': current_scores.get(1, 0),
            'current_p2_score': current_scores.get(2, 0),
            'current_p3_score': current_scores.get(3, 0),
            'current_p4_score': current_scores.get(4, 0),
            'current_p5_score': current_scores.get(5, 0),
            'current_p6_score': current_scores.get(6, 0),
            'current_player': current_player,
            'current_ball': current_ball,
            'game_modes': game_modes
        }
        self.records.append(record)

    def save_log(self):
        with open(self.log_file_path, mode='w', newline='') as file:
            writer = csv.writer(file)
            writer.writerow(["time", "p1", "p2", "p3", "p4", "p5", "p6", "player", "ball", "game_modes"])
            for record in self.records:
                writer.writerow([
                    record['session_time'],
                    record['current_p1_score'],
                    record['current_p2_score'],
                    record['current_p3_score'],
                    record['current_p4_score'],
                    record['current_p5_score'],
                    record['current_p6_score'],
                    record['current_player'],
                    record['current_ball'],
                    record['game_modes']
                ])

    async def send_session_log(self):
        if os.path.exists(self.log_file_path):
            with open(self.log_file_path, 'rb') as log_file:
                # Call the new method in net.py to upload the session log
                await self.net_instance.upload_session_log(self.uuid, self.log_file_path, log_file)
        else:
            raise Exception("Log file does not exist.")