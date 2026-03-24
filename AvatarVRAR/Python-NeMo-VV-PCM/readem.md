da testo in txt in audio

$text = Get-Content .\testo_100_parole.txt -Raw
python tts_client.py --text "$text" --out out_tts.wav --apply_TN

da audio a testo strascritto
python stt_client.py --audio sample.wav --out trascrizione.txt --json_out risposta_stt.json

lista microfoni disponibili
python mic_stt_client.py --list-devices

python -m avatar_client.main

