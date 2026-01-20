#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>
#include <ZXing/ReadBarcode.h>
// qt imports
#include <QCommandLineParser>
#include <QDir>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>
#include <QClipboard>
#include <QApplication>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <QHBoxLayout>
#include <QDateTime>
#include <QImage>
#include <QDesktopServices>
#include <QUrl>
#include <memory>

bool takeScreenshot(const QString& outputPath) {
	int exitCode = QProcess::execute("spectacle", QStringList()
		<< "-b" << "-r" << "-n" << "-o" << outputPath);
	return exitCode == 0;
}

struct OcrResult {
	QString text;
	bool success;
	QString errorMessage;
	bool isQrCode = false;
};

OcrResult detectQrCode(const QString& imagePath) {
	OcrResult result;
	result.success = false;

	QImage image(imagePath);
	if (image.isNull()) {
		result.errorMessage = "Failed to load image for QR detection";
		return result;
	}

	// Convert image to RGB32 to ensure consistent format
	if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
		image = image.convertToFormat(QImage::Format_RGB32);
	}

	ZXing::ReaderOptions options;
	options.setFormats(ZXing::BarcodeFormat::QRCode);
	options.setTryHarder(true);
	options.setTryRotate(true);  // Try rotated images

	const uchar* data = image.constBits();
	int width = image.width();
	int height = image.height();
	int bytesPerLine = image.bytesPerLine();

	// Use ARGB format for RGB32 and ARGB32
	ZXing::ImageFormat format = ZXing::ImageFormat::ARGB;

	ZXing::ImageView imageView(data, width, height, format, bytesPerLine);
	auto zxingResult = ZXing::ReadBarcode(imageView, options);

	if (zxingResult.isValid()) {
		result.text = QString::fromStdString(zxingResult.text());
		result.success = true;
		result.isQrCode = true;
	}
	else {
		result.errorMessage = "Failed to detect valid QR code";
	}

	return result;
}

OcrResult extractText(const QString& imagePath, const QString& language) {
	OcrResult result;
	result.success = true;

	auto ocr = std::make_unique<tesseract::TessBaseAPI>();

	if (ocr->Init(nullptr, language.toUtf8().constData())) {
		result.success = false;
		result.errorMessage =
			"Error initializing Tesseract OCR for language: " + language;
		return result;
	}

	Pix* image = pixRead(imagePath.toUtf8().constData());
	if (!image) {
		ocr->End();
		result.success = false;
		result.errorMessage = "Failed to load image";
		return result;
	}

	ocr->SetImage(image);

	char* outText = ocr->GetUTF8Text();
	result.text = QString::fromUtf8(outText);

	delete[] outText;
	pixDestroy(&image);
	ocr->End();

	return result;
}

int main(int argc, char* argv[]) {
	QApplication app(argc, argv);

	QCommandLineParser parser;
	parser.setApplicationDescription("Extract text from spectacle screenshots using OCR");
	parser.addHelpOption();

	QCommandLineOption langOption(
		QStringList() << "lang",
		"Language(s) for OCR (e.g., eng, hin, or eng+hin for multiple languages)",
		"language", "eng");

	QCommandLineOption disable_qr(
		QStringList() << "disable-qr",
		"Disable QR code detection and extraction.");

	QCommandLineOption webBrowserOption(
		QStringList() << "web" << "browser",
		"Open OCR results in web browser.");

	parser.addOption(langOption);
	parser.addOption(disable_qr);
	parser.addOption(webBrowserOption);
	parser.process(app);

	QString language = parser.value(langOption);

	// Check if web browser output is requested
	bool openInBrowser = parser.isSet(webBrowserOption);

	QWidget window;
	window.setWindowTitle("Spectacle Screenshot OCR - Language: " + language);
	window.resize(500, 400);

	QVBoxLayout* layout = new QVBoxLayout();

	QLabel* label = new QLabel();
	layout->addWidget(label);

	QTextEdit* textEdit = new QTextEdit();
	textEdit->setMinimumHeight(100);
	layout->addWidget(textEdit);

	QWidget* buttonContainer = new QWidget();
	QHBoxLayout* buttonLayout = new QHBoxLayout(buttonContainer);

	QPushButton* copyButton = new QPushButton("Copy Text");
	QPushButton* saveButton = new QPushButton("Save Text");
	QPushButton* saveImageButton = new QPushButton("Save Image");
	QPushButton* browserButton = new QPushButton("Open in Browser");

	buttonLayout->addWidget(copyButton);
	buttonLayout->addWidget(saveButton);
	buttonLayout->addWidget(saveImageButton);
	buttonLayout->addWidget(browserButton);
	layout->addWidget(buttonContainer);

	window.setLayout(layout);

	QString tempPath = QDir::tempPath() + "/screenshot.png";

	QObject::connect(copyButton, &QPushButton::clicked, [&]() {
		if (!textEdit->toPlainText().isEmpty()) {
			QApplication::clipboard()->setText(textEdit->toPlainText());
			label->setText("Text copied to clipboard");
		}
		else {
			label->setText("No text to copy");
		}
		});

	QObject::connect(saveButton, &QPushButton::clicked, [&]() {
		if (!textEdit->toPlainText().isEmpty()) {
			QString fileName = QFileDialog::getSaveFileName(
				&window, "Save OCR Text", QDir::homePath(),
				"Text Files (*.txt);;All Files (*)");

			if (!fileName.isEmpty()) {
				QFile file(fileName);
				if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
					QTextStream out(&file);
					out << textEdit->toPlainText();
					file.close();
					label->setText("Text saved to file");
				}
				else {
					label->setText("Failed to save file");
					QMessageBox::critical(&window, "Error", "Failed to save the file");
				}
			}
		}
		else {
			label->setText("No text to save");
		}
		});

	QObject::connect(saveImageButton, &QPushButton::clicked, [&]() {
		QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
		QString defaultImageName = QDir::homePath() + "/Screenshot_" + timestamp;
		QString imageFileName = QFileDialog::getSaveFileName(
			&window, "Save Screenshot", defaultImageName,
			"Image Files (*.png);;All Files (*)");
		if (!imageFileName.isEmpty()) {
			if (QFile::copy(tempPath, imageFileName))
				label->setText("Screenshot saved successfully");
			else {
				label->setText("Failed to save screenshot");
				QMessageBox::critical(&window, "Error", "Failed to save the screenshot file");
			}
		}
		});

	QObject::connect(browserButton, &QPushButton::clicked, [&]() {
		if (!textEdit->toPlainText().isEmpty()) {
			// Create a temporary HTML file with the OCR results
			QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
			QString htmlPath = QDir::tempPath() + "/ocr_result_" + timestamp + ".html";
			
			QFile file(htmlPath);
			if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
				QTextStream out(&file);
				out << "<!DOCTYPE html>\n"
					<< "<html lang=\"en\">\n"
					<< "<head>\n"
					<< "    <meta charset=\"UTF-8\">\n"
					<< "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
					<< "    <title>OCR Results</title>\n"
					<< "    <style>\n"
					<< "        body {\n"
					<< "            font-family: Arial, sans-serif;\n"
					<< "            margin: 20px;\n"
					<< "            line-height: 1.6;\n"
					<< "            background-color: #f4f4f4;\n"
					<< "        }\n"
					<< "        .container {\n"
					<< "            max-width: 800px;\n"
					<< "            margin: 0 auto;\n"
					<< "            background-color: white;\n"
					<< "            padding: 20px;\n"
					<< "            border-radius: 8px;\n"
					<< "            box-shadow: 0 2px 4px rgba(0,0,0,0.1);\n"
					<< "        }\n"
					<< "        h1 {\n"
					<< "            color: #333;\n"
					<< "        }\n"
					<< "        .timestamp {\n"
					<< "            color: #666;\n"
					<< "            font-size: 0.9em;\n"
					<< "        }\n"
					<< "        .content {\n"
					<< "            width: 100%;\n"
					<< "            min-height: 300px;\n"
					<< "            padding: 15px;\n"
					<< "            border: 2px solid #007bff;\n"
					<< "            border-radius: 4px;\n"
					<< "            font-family: 'Courier New', monospace;\n"
					<< "            font-size: 14px;\n"
					<< "            box-sizing: border-box;\n"
					<< "            resize: vertical;\n"
					<< "        }\n"
					<< "        .button-group {\n"
					<< "            margin-top: 15px;\n"
					<< "            display: flex;\n"
					<< "            gap: 10px;\n"
					<< "        }\n"
					<< "        button {\n"
					<< "            padding: 10px 20px;\n"
					<< "            background-color: #007bff;\n"
					<< "            color: white;\n"
					<< "            border: none;\n"
					<< "            border-radius: 4px;\n"
					<< "            cursor: pointer;\n"
					<< "            font-size: 14px;\n"
					<< "        }\n"
					<< "        button:hover {\n"
					<< "            background-color: #0056b3;\n"
					<< "        }\n"
					<< "    </style>\n"
					<< "</head>\n"
					<< "<body>\n"
					<< "    <div class=\"container\">\n"
					<< "        <h1>OCR Results</h1>\n"
					<< "        <p class=\"timestamp\">Generated: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "</p>\n"
					<< "        <textarea id=\"content\" class=\"content\">" << textEdit->toPlainText().toHtmlEscaped() << "</textarea>\n"
					<< "        <div class=\"button-group\">\n"
					<< "            <button onclick=\"copyText()\">Copy to Clipboard</button>\n"
					<< "            <button onclick=\"downloadText()\">Download as TXT</button>\n"
					<< "        </div>\n"
					<< "    </div>\n"
					<< "    <script>\n"
					<< "        function copyText() {\n"
					<< "            const textarea = document.getElementById('content');\n"
					<< "            textarea.select();\n"
					<< "            document.execCommand('copy');\n"
					<< "            alert('Text copied to clipboard!');\n"
					<< "        }\n"
					<< "        function downloadText() {\n"
					<< "            const textarea = document.getElementById('content');\n"
					<< "            const text = textarea.value;\n"
					<< "            const element = document.createElement('a');\n"
					<< "            element.setAttribute('href', 'data:text/plain;charset=utf-8,' + encodeURIComponent(text));\n"
					<< "            element.setAttribute('download', 'ocr_result.txt');\n"
					<< "            element.style.display = 'none';\n"
					<< "            document.body.appendChild(element);\n"
					<< "            element.click();\n"
					<< "            document.body.removeChild(element);\n"
					<< "        }\n"
					<< "    </script>\n"
					<< "</body>\n"
					<< "</html>\n";
				file.close();
				
				// Open the HTML file in the default web browser
				if (QDesktopServices::openUrl(QUrl::fromLocalFile(htmlPath))) {
					label->setText("OCR results opened in web browser");
				} else {
					label->setText("Failed to open web browser");
					QMessageBox::warning(&window, "Warning", "Could not open default web browser");
				}
			} else {
				label->setText("Failed to create HTML file");
				QMessageBox::critical(&window, "Error", "Failed to create temporary HTML file");
			}
		}
		else {
			label->setText("No text to display");
		}
		});

	if (takeScreenshot(tempPath)) {
		OcrResult result;
		if (!parser.isSet(disable_qr)) {
			result = detectQrCode(tempPath);
			if (result.success) {
				textEdit->setText(result.text);
				label->setText("QR code detected and decoded successfully");
				
				// Auto-open in browser if requested
				if (openInBrowser) {
					QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
					QString htmlPath = QDir::tempPath() + "/ocr_result_" + timestamp + ".html";
					
					QFile file(htmlPath);
					if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
						QTextStream out(&file);
						out << "<!DOCTYPE html>\n"
							<< "<html lang=\"en\">\n"
							<< "<head>\n"
							<< "    <meta charset=\"UTF-8\">\n"
							<< "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
							<< "    <title>OCR Results</title>\n"
							<< "    <style>\n"
							<< "        body {\n"
							<< "            font-family: Arial, sans-serif;\n"
							<< "            margin: 20px;\n"
							<< "            line-height: 1.6;\n"
							<< "            background-color: #f4f4f4;\n"
							<< "        }\n"
							<< "        .container {\n"
							<< "            max-width: 800px;\n"
							<< "            margin: 0 auto;\n"
							<< "            background-color: white;\n"
							<< "            padding: 20px;\n"
							<< "            border-radius: 8px;\n"
							<< "            box-shadow: 0 2px 4px rgba(0,0,0,0.1);\n"
							<< "        }\n"
							<< "        h1 {\n"
							<< "            color: #333;\n"
							<< "        }\n"
							<< "        .timestamp {\n"
							<< "            color: #666;\n"
							<< "            font-size: 0.9em;\n"
							<< "        }\n"
							<< "        .content {\n"
							<< "            width: 100%;\n"
							<< "            min-height: 300px;\n"
							<< "            padding: 15px;\n"
							<< "            border: 2px solid #007bff;\n"
							<< "            border-radius: 4px;\n"
							<< "            font-family: 'Courier New', monospace;\n"
							<< "            font-size: 14px;\n"
							<< "            box-sizing: border-box;\n"
							<< "            resize: vertical;\n"
							<< "        }\n"
							<< "        .button-group {\n"
							<< "            margin-top: 15px;\n"
							<< "            display: flex;\n"
							<< "            gap: 10px;\n"
							<< "        }\n"
							<< "        button {\n"
							<< "            padding: 10px 20px;\n"
							<< "            background-color: #007bff;\n"
							<< "            color: white;\n"
							<< "            border: none;\n"
							<< "            border-radius: 4px;\n"
							<< "            cursor: pointer;\n"
							<< "            font-size: 14px;\n"
							<< "        }\n"
							<< "        button:hover {\n"
							<< "            background-color: #0056b3;\n"
							<< "        }\n"
							<< "    </style>\n"
							<< "</head>\n"
							<< "<body>\n"
							<< "    <div class=\"container\">\n"
							<< "        <h1>OCR Results</h1>\n"
							<< "        <p class=\"timestamp\">Generated: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "</p>\n"
							<< "        <textarea id=\"content\" class=\"content\">" << result.text.toHtmlEscaped() << "</textarea>\n"
							<< "        <div class=\"button-group\">\n"
							<< "            <button onclick=\"copyText()\">Copy to Clipboard</button>\n"
							<< "            <button onclick=\"downloadText()\">Download as TXT</button>\n"
							<< "        </div>\n"
							<< "    </div>\n"
							<< "    <script>\n"
							<< "        function copyText() {\n"
							<< "            const textarea = document.getElementById('content');\n"
							<< "            textarea.select();\n"
							<< "            document.execCommand('copy');\n"
							<< "            alert('Text copied to clipboard!');\n"
							<< "        }\n"
							<< "        function downloadText() {\n"
							<< "            const textarea = document.getElementById('content');\n"
							<< "            const text = textarea.value;\n"
							<< "            const element = document.createElement('a');\n"
							<< "            element.setAttribute('href', 'data:text/plain;charset=utf-8,' + encodeURIComponent(text));\n"
							<< "            element.setAttribute('download', 'ocr_result.txt');\n"
							<< "            element.style.display = 'none';\n"
							<< "            document.body.appendChild(element);\n"
							<< "            element.click();\n"
							<< "            document.body.removeChild(element);\n"
							<< "        }\n"
							<< "    </script>\n"
							<< "</body>\n"
							<< "</html>\n";
						file.close();
						QDesktopServices::openUrl(QUrl::fromLocalFile(htmlPath));
					}
					return 0;
				}
				
				window.show();
				return app.exec();
			}
		}

		result = extractText(tempPath, language);
		if (!result.success) {
			textEdit->setText("");
			label->setText(result.errorMessage);
		}
		else {
			textEdit->setText(result.text);
			label->setText("Text extracted successfully.");
			
			// Auto-open in browser if requested
			if (openInBrowser) {
				QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
				QString htmlPath = QDir::tempPath() + "/ocr_result_" + timestamp + ".html";
				
				QFile file(htmlPath);
				if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
					QTextStream out(&file);
					out << "<!DOCTYPE html>\n"
						<< "<html lang=\"en\">\n"
						<< "<head>\n"
						<< "    <meta charset=\"UTF-8\">\n"
						<< "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
						<< "    <title>OCR Results</title>\n"
						<< "    <style>\n"
						<< "        body {\n"
						<< "            font-family: Arial, sans-serif;\n"
						<< "            margin: 20px;\n"
						<< "            line-height: 1.6;\n"
						<< "            background-color: #f4f4f4;\n"
						<< "        }\n"
						<< "        .container {\n"
						<< "            max-width: 800px;\n"
						<< "            margin: 0 auto;\n"
						<< "            background-color: white;\n"
						<< "            padding: 20px;\n"
						<< "            border-radius: 8px;\n"
						<< "            box-shadow: 0 2px 4px rgba(0,0,0,0.1);\n"
						<< "        }\n"
						<< "        h1 {\n"
						<< "            color: #333;\n"
						<< "        }\n"
						<< "        .timestamp {\n"
						<< "            color: #666;\n"
						<< "            font-size: 0.9em;\n"
						<< "        }\n"
						<< "        .content {\n"
						<< "            width: 100%;\n"
						<< "            min-height: 300px;\n"
						<< "            padding: 15px;\n"
						<< "            border: 2px solid #007bff;\n"
						<< "            border-radius: 4px;\n"
						<< "            font-family: 'Courier New', monospace;\n"
						<< "            font-size: 14px;\n"
						<< "            box-sizing: border-box;\n"
						<< "            resize: vertical;\n"
						<< "        }\n"
						<< "        .button-group {\n"
						<< "            margin-top: 15px;\n"
						<< "            display: flex;\n"
						<< "            gap: 10px;\n"
						<< "        }\n"
						<< "        button {\n"
						<< "            padding: 10px 20px;\n"
						<< "            background-color: #007bff;\n"
						<< "            color: white;\n"
						<< "            border: none;\n"
						<< "            border-radius: 4px;\n"
						<< "            cursor: pointer;\n"
						<< "            font-size: 14px;\n"
						<< "        }\n"
						<< "        button:hover {\n"
						<< "            background-color: #0056b3;\n"
						<< "        }\n"
						<< "    </style>\n"
						<< "</head>\n"
						<< "<body>\n"
						<< "    <div class=\"container\">\n"
						<< "        <h1>OCR Results</h1>\n"
						<< "        <p class=\"timestamp\">Generated: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "</p>\n"
						<< "        <textarea id=\"content\" class=\"content\">" << result.text.toHtmlEscaped() << "</textarea>\n"
						<< "        <div class=\"button-group\">\n"
						<< "            <button onclick=\"copyText()\">Copy to Clipboard</button>\n"
						<< "            <button onclick=\"downloadText()\">Download as TXT</button>\n"
						<< "        </div>\n"
						<< "    </div>\n"
						<< "    <script>\n"
						<< "        function copyText() {\n"
						<< "            const textarea = document.getElementById('content');\n"
						<< "            textarea.select();\n"
						<< "            document.execCommand('copy');\n"
						<< "            alert('Text copied to clipboard!');\n"
						<< "        }\n"
						<< "        function downloadText() {\n"
						<< "            const textarea = document.getElementById('content');\n"
						<< "            const text = textarea.value;\n"
						<< "            const element = document.createElement('a');\n"
						<< "            element.setAttribute('href', 'data:text/plain;charset=utf-8,' + encodeURIComponent(text));\n"
						<< "            element.setAttribute('download', 'ocr_result.txt');\n"
						<< "            element.style.display = 'none';\n"
						<< "            document.body.appendChild(element);\n"
						<< "            element.click();\n"
						<< "            document.body.removeChild(element);\n"
						<< "        }\n"
						<< "    </script>\n"
						<< "</body>\n"
						<< "</html>\n";
					file.close();
					QDesktopServices::openUrl(QUrl::fromLocalFile(htmlPath));
				}
				return 0;
			}
		}
		window.show();
	}
	else {
		textEdit->setText("");
		label->setText("Error occurred while taking screenshot");
		window.show();
		QMessageBox::critical(&window, "Error",
			"Failed to launch Spectacle or take screenshot");
	}

	return app.exec();
}
