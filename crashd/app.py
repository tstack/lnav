from werkzeug.middleware.proxy_fix import ProxyFix
from flask import Flask, request, send_file, after_this_request
import os
import glob
import uuid
from datetime import datetime
import zipfile
import spookyhash

app = Flask(__name__)
app.wsgi_app = ProxyFix(
    app.wsgi_app, x_for=2, x_proto=2, x_host=2, x_prefix=2
)

ROOT_PAGE = """
<html>
<head>
<title>lnav crash upload site</title>
</head>

<body>
You can help improve <b>lnav</b> by uploading your crash logs by running:
<pre>
lnav -m crash upload
</pre>
</body>
</html>
"""


# Function to check free space on filesystem
def check_free_space():
    statvfs = os.statvfs('/logs')
    # Calculate free space in MB
    free_space_mb = (statvfs.f_bsize * statvfs.f_bavail) / (1024 * 1024)
    return free_space_mb


@app.route('/crash', methods=['POST'])
def crash_handler():
    # Check free space on filesystem
    free_space_mb = check_free_space()
    if free_space_mb < 100:
        return 'Insufficient free space on the filesystem!\n', 500

    # Retrieve the secret value from the environment variable
    lnav_secret_env = os.environ.get('LNAV_SECRET')

    # Check if header 'lnav-secret' exists and has the correct value
    if request.headers.get('lnav-secret') == lnav_secret_env:
        # Check if content length is provided
        if 'Content-Length' not in request.headers:
            return 'Content length header is missing!', 400

        # Get the content length
        content_length = int(request.headers['Content-Length'])

        # Check if content length is zero
        if content_length == 0:
            return 'Empty request body!', 400

        # Check if content length exceeds 10MB
        if content_length > 10 * 1024 * 1024:  # 10MB limit
            return 'Content size exceeds the limit of 10MB!', 413

        # Get the content from the request body
        content = request.data

        nonce = request.headers.get('X-lnav-nonce', '')
        crash_hash = request.headers.get('X-lnav-hash', '')
        if not crash_hash.startswith("0000"):
            return "Invalid proof of work hash", 401

        sh = spookyhash.Hash128()
        sh.update(nonce.encode('utf-8'))
        sh.update(content)
        verify_hash = sh.hexdigest()
        if verify_hash != crash_hash:
            return "Proof of work hash does not match", 401

        # Generate a unique ID
        unique_id = str(uuid.uuid4())

        # Get the current time
        current_time = datetime.now().strftime('%Y-%m-%d_%H-%M-%S')

        # Construct the file name with current time and unique ID
        file_name = f'crash_log_{current_time}_{unique_id}.txt'
        full_path = os.path.join("/logs", file_name)

        # Save the content to the file in the current directory
        with open(full_path, 'wb') as f:
            f.write(content)

        return 'Data saved successfully!', 200
    else:
        return 'Unauthorized access!', 401


@app.route('/download_crashes', methods=['GET'])
def download_crashes():
    # Retrieve the secret value for downloading from the environment variable
    lnav_download_secret_env = os.environ.get('LNAV_DOWNLOAD_SECRET')

    # Check if header 'lnav-secret' exists and has the correct value for downloading
    if request.headers.get('lnav-secret') == lnav_download_secret_env:
        # Get all the files in the current directory
        crash_files = glob.glob("/logs/crash_log_*")

        # Generate a unique ID for the zip file
        zip_id = str(uuid.uuid4())

        # Construct the zip file name
        zip_file_name = f'crash_archive_{zip_id}.zip'

        # Create a new zip file
        with zipfile.ZipFile(zip_file_name, 'w') as zipf:
            # Add each crash file to the zip file
            for crash_file in crash_files:
                zipf.write(crash_file)

        # Delete the crash files
        for crash_file in crash_files:
            os.remove(crash_file)

        @after_this_request
        def remove_zip(response):
            try:
                os.remove(zip_file_name)
            except Exception as e:
                app.logger.error(f"Error removing zip file: {e}")
            return response

        # Send the zip file as a response
        return send_file(zip_file_name, as_attachment=True, download_name=zip_file_name)

    else:
        return 'Unauthorized access!\n', 401


@app.route('/', methods=['GET'])
def root_page():
    return ROOT_PAGE, 200


if __name__ == '__main__':
    # Run the Flask app
    app.run()
