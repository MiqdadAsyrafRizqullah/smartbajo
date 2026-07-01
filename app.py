import os
import io
import json
import random
from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS
from PIL import Image
import numpy as np

# Set environment variable to suppress TensorFlow logging
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '2'

import tensorflow as tf

app = Flask(__name__, static_folder='smart-bajo-main', static_url_path='')
CORS(app)

MODEL_DIR = 'output model smartbajo'

# Definitions for plants and their labels (based on user prompt and pkl files)
PLANT_INFO = {
    'bayam': {
        'model_path': os.path.join(MODEL_DIR, 'model_bayam.h5'),
        'labels': ['antraknosa lubang daun', 'sehat'],  # Based on prompt
        'recommendations': {
            'sehat': 'Lanjutkan dosing nutrisi dengan komposisi saat ini. Jaga sirkulasi udara.',
            'antraknosa lubang daun': 'Gunakan fungisida organik. Kurangi kelembaban dan hindari penyiraman pada daun berlebih.'
        }
    },
    'kangkung': {
        'model_path': os.path.join(MODEL_DIR, 'model_kangkung.h5'),
        'labels': ['bercak daun', 'daun menguning', 'sehat'], # Assuming alphabetical or checking model
        'recommendations': {
            'sehat': 'Kondisi optimal. Pertahankan nutrisi pada EC 2.0 - 2.5 mS/cm.',
            'bercak daun': 'Buang daun yang terinfeksi. Pastikan sirkulasi udara baik dan pertimbangkan aplikasi fungisida.',
            'daun menguning': 'Gejala defisiensi nitrogen. Tambahkan nutrisi AB Mix khusus daun dan cek pH air.'
        }
    },
    'selada': {
        'model_path': os.path.join(MODEL_DIR, 'model_selada.h5'),
        'labels': ['penyakit bakteri', 'penyakit jamur', 'sehat'],
        'recommendations': {
            'sehat': 'Tanaman sehat. Pertahankan suhu air di bawah 28°C untuk mencegah tipburn.',
            'penyakit bakteri': 'Karantina tanaman yang sakit. Sterilisasi sistem hidroponik dan jaga kebersihan rakit.',
            'penyakit jamur': 'Gunakan bio-fungisida (Trichoderma). Kurangi kelembaban di sekitar kanopi tanaman.'
        }
    },
    'pakcoy': {
        'model_path': os.path.join(MODEL_DIR, 'model_pakcoy.h5'),
        'labels': ['bercak hitam', 'daun menguning', 'sehat'],
        'recommendations': {
            'sehat': 'Pertumbuhan baik. Jaga intensitas cahaya matahari untuk mencegah etiolasi.',
            'bercak hitam': 'Ini bisa jadi indikasi serangan jamur Alternaria. Semprotkan fungisida nabati.',
            'daun menguning': 'Periksa level EC (kekurangan nutrisi) atau pastikan akar tidak busuk (cek aerasi air).'
        }
    },
    'sawi': {
        'model_path': os.path.join(MODEL_DIR, 'model_sawi.h5'),
        'labels': ['busuk daun', 'leaf miner', 'sawi sehat', 'ulat grayak'],
        'recommendations': {
            'sawi sehat': 'Tanaman sangat sehat. Teruskan jadwal pemberian nutrisi secara konsisten.',
            'busuk daun': 'Tingkatkan aerasi pada akar (tambah airstone) dan pastikan suhu larutan tidak terlalu panas.',
            'leaf miner': 'Buang daun yang terkena. Gunakan perangkap kuning (yellow trap) untuk lalat dewasa.',
            'ulat grayak': 'Lakukan pembersihan manual (ambil ulat) atau gunakan biopestisida Bacillus thuringiensis (Bt).'
        }
    }
}

# Cache for loaded models to avoid loading on every request
loaded_models = {}

def get_model(plant_type):
    if plant_type not in loaded_models:
        model_path = PLANT_INFO[plant_type]['model_path']
        print(f"Loading model for {plant_type} from {model_path}...")
        try:
            loaded_models[plant_type] = tf.keras.models.load_model(model_path)
            print(f"Model for {plant_type} loaded successfully.")
        except Exception as e:
            print(f"Error loading model for {plant_type}: {e}")
            return None
    return loaded_models[plant_type]

def preprocess_image(image, target_size=(224, 224)):
    if image.mode != 'RGB':
        image = image.convert('RGB')
    image = image.resize(target_size)
    img_array = np.array(image)
    img_array = np.expand_dims(img_array, axis=0)
    img_array = img_array.astype('float32') / 255.0
    return img_array

@app.route('/')
def index():
    return app.send_static_file('index.html')

@app.route('/<path:path>')
def static_proxy(path):
    return app.send_static_file(path)

@app.route('/api/predict', methods=['POST'])
def predict():
    if 'image' not in request.files:
        return jsonify({'error': 'No image uploaded'}), 400
    
    plant_type = request.form.get('plant', '').lower()
    if plant_type not in PLANT_INFO:
        return jsonify({'error': 'Invalid plant type'}), 400

    file = request.files['image']
    try:
        img = Image.open(io.BytesIO(file.read()))
        img_array = preprocess_image(img)
    except Exception as e:
        return jsonify({'error': f'Error processing image: {e}'}), 400

    model = get_model(plant_type)
    if model is None:
        return jsonify({'error': 'Model could not be loaded'}), 500

    try:
        predictions = model.predict(img_array)
        predicted_index = np.argmax(predictions[0])
        confidence = float(predictions[0][predicted_index])
        
        # Load labels dynamically if pkl exists, else use hardcoded
        labels_path = os.path.join(MODEL_DIR, f"labels_{plant_type}.pkl")
        if os.path.exists(labels_path):
            import pickle
            with open(labels_path, 'rb') as f:
                labels = pickle.load(f)
        else:
            labels = PLANT_INFO[plant_type]['labels']

        predicted_class = labels[predicted_index]
        
        # Determine status (healthy vs issue)
        is_healthy = 'sehat' in predicted_class.lower()
        
        # Get recommendation
        recommendation = PLANT_INFO[plant_type]['recommendations'].get(predicted_class, 'Tidak ada saran spesifik.')

        return jsonify({
            'plant': plant_type.capitalize(),
            'class': predicted_class.capitalize(),
            'confidence': round(confidence * 100, 2),
            'is_healthy': is_healthy,
            'recommendation': recommendation
        })

    except Exception as e:
        return jsonify({'error': f'Error during prediction: {e}'}), 500

@app.route('/api/sensors', methods=['GET'])
def get_sensors():
    # Simulated ESP32 Sensor Data (Real implementation would fetch from MQTT/InfluxDB)
    return jsonify({
        'suhu_air': round(random.uniform(26.0, 31.5), 2),
        'ec': round(random.uniform(1.8, 3.2), 2),
        'tds': round(random.uniform(1100, 1800), 2),
        'suhu_udara': round(random.uniform(29.0, 35.0), 2),
        'kelembaban': round(random.uniform(60.0, 90.0), 2),
        'kualitas': 'Baik (Sistem Hidroponik)'
    })

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
