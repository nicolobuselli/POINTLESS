#include "imagefilewidget.h"
#include <QVBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QLabel>

ImageFileWidget::ImageFileWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    QPushButton* btn = new QPushButton("Carica Immagine", this);
    QLabel* label = new QLabel("Nessun file selezionato", this);
    layout->addWidget(btn);
    layout->addWidget(label);
    connect(btn, &QPushButton::clicked, this, [this, label]() {
        QString file = QFileDialog::getOpenFileName(this, "Seleziona immagine", QString(), "Immagini (*.png *.jpg *.jpeg *.bmp)");
        if (!file.isEmpty()) {
            QImage img(file);
            if (!img.isNull()) {
                label->setText(file);
                emit imageLoaded(img);
            }
        }
    });
}

QImage ImageFileWidget::image() const { return QImage(); /* placeholder */ }
QString ImageFileWidget::filePath() const { return QString(); /* placeholder */ }
