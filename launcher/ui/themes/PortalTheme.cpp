// SPDX-License-Identifier: GPL-3.0-only
#include "PortalTheme.h"

#include <QObject>

// The Portal palette, drawn from the logo: obsidian block purples for the
// surfaces, the vortex violet for selection and actions, the magenta crack
// glow reserved for small accents. Values are stepped so depth reads from
// color alone: well #100d17 < window #171420 < raised #251f33 < hover #332b45.

QString PortalTheme::id()
{
    return "portal";
}

QString PortalTheme::name()
{
    return QObject::tr("Portal");
}

QString PortalTheme::tooltip()
{
    return QObject::tr("The Portal look: obsidian purple with a violet glow.");
}

QPalette PortalTheme::colorScheme()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x17, 0x14, 0x20));
    palette.setColor(QPalette::WindowText, QColor(0xe2, 0xdd, 0xee));
    palette.setColor(QPalette::Base, QColor(0x10, 0x0d, 0x17));
    palette.setColor(QPalette::AlternateBase, QColor(0x1c, 0x17, 0x28));
    palette.setColor(QPalette::ToolTipBase, QColor(0x22, 0x1c, 0x31));
    palette.setColor(QPalette::ToolTipText, QColor(0xe2, 0xdd, 0xee));
    palette.setColor(QPalette::Text, QColor(0xe2, 0xdd, 0xee));
    palette.setColor(QPalette::Button, QColor(0x25, 0x1f, 0x33));
    palette.setColor(QPalette::ButtonText, QColor(0xe9, 0xe4, 0xf4));
    palette.setColor(QPalette::BrightText, QColor(0xff, 0x4f, 0xd8));
    palette.setColor(QPalette::Link, QColor(0xb0, 0x6d, 0xff));
    palette.setColor(QPalette::Highlight, QColor(0x7c, 0x3a, 0xed));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, QColor(0x87, 0x7e, 0x9b));
    return fadeInactive(palette, fadeAmount(), fadeColor());
}

double PortalTheme::fadeAmount()
{
    return 0.5;
}

QColor PortalTheme::fadeColor()
{
    return QColor(0x17, 0x14, 0x20);
}

bool PortalTheme::hasStyleSheet()
{
    return true;
}

QString PortalTheme::appStyleSheet()
{
    return QStringLiteral(R"qss(
QToolTip {
    color: #e2ddee;
    background-color: #221c31;
    border: 1px solid #45376b;
    border-radius: 4px;
    padding: 4px 8px;
}

/* Buttons: raised obsidian with a violet reply on hover; the default
   button carries the vortex violet. */
QPushButton {
    background-color: #2a2338;
    color: #e9e4f4;
    border: 1px solid #3c3352;
    border-radius: 5px;
    padding: 4px 12px;
}
QPushButton:hover {
    background-color: #332b45;
    border-color: #5b4a85;
}
QPushButton:pressed {
    background-color: #201a2e;
}
QPushButton:checked {
    background-color: #3b2f56;
    border-color: #7c3aed;
}
QPushButton:default {
    background-color: #7c3aed;
    color: #ffffff;
    border: 1px solid #8b52f0;
}
QPushButton:default:hover {
    background-color: #8b52f0;
}
QPushButton:default:pressed {
    background-color: #6b2fd4;
}
QPushButton:disabled {
    background-color: #1d1829;
    color: #6b6280;
    border-color: #2c2540;
}

/* Inputs: dark wells with a violet focus ring */
QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background-color: #100d17;
    color: #e2ddee;
    border: 1px solid #372f4c;
    border-radius: 5px;
    padding: 3px 6px;
    selection-background-color: #7c3aed;
    selection-color: #ffffff;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border-color: #8b5cf6;
}
QLineEdit:disabled, QPlainTextEdit:disabled, QTextEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled {
    color: #6b6280;
    border-color: #2c2540;
}
QAbstractSpinBox QLineEdit, QComboBox QLineEdit {
    background: transparent;
    border: none;
    padding: 0px;
}
QComboBox::drop-down {
    background: transparent;
    border: none;
    width: 20px;
}
QComboBox QAbstractItemView {
    background-color: #1d1830;
    color: #e2ddee;
    border: 1px solid #3c3352;
    border-radius: 6px;
    selection-background-color: #7c3aed;
    selection-color: #ffffff;
    outline: none;
}

/* Menus */
QMenu {
    background-color: #1d1830;
    color: #e2ddee;
    border: 1px solid #3c3352;
    border-radius: 8px;
    padding: 4px;
}
QMenu::item {
    padding: 5px 22px 5px 10px;
    border-radius: 4px;
    margin: 1px 2px;
}
QMenu::item:selected {
    background-color: #7c3aed;
    color: #ffffff;
}
QMenu::item:disabled {
    color: #6b6280;
}
QMenu::separator {
    height: 1px;
    background: #322a48;
    margin: 4px 8px;
}
QMenuBar {
    background-color: #171420;
    color: #e2ddee;
}
QMenuBar::item {
    padding: 4px 8px;
    border-radius: 4px;
}
QMenuBar::item:selected {
    background-color: #2a2338;
}

/* Tabs: quiet tabs with a violet underline on the active one */
QTabWidget::pane {
    border: 1px solid #2c2540;
    border-radius: 4px;
    top: -1px;
}
QTabBar::tab {
    background: transparent;
    color: #a99fc0;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 5px 12px;
}
QTabBar::tab:hover {
    color: #e2ddee;
    border-bottom-color: #45376b;
}
QTabBar::tab:selected {
    color: #ffffff;
    border-bottom-color: #8b5cf6;
}

/* Scrollbars: slim violet-gray pills, no buttons */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px;
}
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 2px;
}
QScrollBar::handle:vertical {
    background: #3a3151;
    border-radius: 3px;
    min-height: 24px;
}
QScrollBar::handle:horizontal {
    background: #3a3151;
    border-radius: 3px;
    min-width: 24px;
}
QScrollBar::handle:hover, QScrollBar::handle:pressed {
    background: #4d4170;
}
QScrollBar::add-line, QScrollBar::sub-line {
    width: 0px;
    height: 0px;
    background: none;
}
QScrollBar::add-page, QScrollBar::sub-page {
    background: transparent;
}
QAbstractScrollArea::corner {
    background: transparent;
}

/* Item views: violet selection, soft violet hover */
QListView, QTreeView, QTableView {
    background-color: #100d17;
    alternate-background-color: #16121f;
    border: 1px solid #2c2540;
    border-radius: 4px;
    outline: none;
}
QListView::item:hover, QTreeView::item:hover, QTableView::item:hover {
    background: rgba(124, 58, 237, 40);
}
QListView::item:selected, QTreeView::item:selected, QTableView::item:selected {
    background: #7c3aed;
    color: #ffffff;
}
QHeaderView::section {
    background-color: #1d1830;
    color: #a99fc0;
    border: none;
    border-bottom: 1px solid #3c3352;
    border-right: 1px solid #2c2540;
    padding: 4px 8px;
}
QTableCornerButton::section {
    background-color: #1d1830;
    border: none;
}

/* Group boxes */
QGroupBox {
    border: 1px solid #2e2740;
    border-radius: 6px;
    margin-top: 12px;
    padding-top: 4px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 8px;
    padding: 0px 4px;
    color: #b8aed0;
}

/* Progress: violet on a dark well */
QProgressBar {
    background-color: #100d17;
    border: 1px solid #2c2540;
    border-radius: 4px;
    text-align: center;
    color: #e2ddee;
}
QProgressBar::chunk {
    background-color: #7c3aed;
    border-radius: 3px;
}

/* Checkboxes and radios: violet when set */
QCheckBox, QRadioButton {
    spacing: 6px;
}
QCheckBox::indicator, QGroupBox::indicator, QTreeView::indicator, QListView::indicator, QTableView::indicator {
    width: 15px;
    height: 15px;
    background-color: #100d17;
    border: 1px solid #453a63;
    border-radius: 3px;
}
QCheckBox::indicator:hover, QGroupBox::indicator:hover, QTreeView::indicator:hover, QListView::indicator:hover, QTableView::indicator:hover {
    border-color: #8b5cf6;
}
QCheckBox::indicator:checked, QGroupBox::indicator:checked, QTreeView::indicator:checked, QListView::indicator:checked, QTableView::indicator:checked {
    border-color: #8b5cf6;
    background-color: qradialgradient(cx: 0.5, cy: 0.5, radius: 0.5, fx: 0.5, fy: 0.5, stop: 0 #ffffff, stop: 0.35 #ffffff, stop: 0.45 #7c3aed, stop: 1 #7c3aed);
}
QCheckBox::indicator:indeterminate, QTreeView::indicator:indeterminate, QListView::indicator:indeterminate {
    border-color: #8b5cf6;
    background-color: #3b2f56;
}
QRadioButton::indicator {
    width: 15px;
    height: 15px;
    background-color: #100d17;
    border: 1px solid #453a63;
    border-radius: 8px;
}
QRadioButton::indicator:hover {
    border-color: #8b5cf6;
}
QRadioButton::indicator:checked {
    border-color: #8b5cf6;
    background-color: qradialgradient(cx: 0.5, cy: 0.5, radius: 0.5, fx: 0.5, fy: 0.5, stop: 0 #7c3aed, stop: 0.4 #7c3aed, stop: 0.5 #100d17, stop: 1 #100d17);
}

/* Sliders */
QSlider::groove:horizontal {
    height: 4px;
    background: #2a2338;
    border-radius: 2px;
}
QSlider::sub-page:horizontal {
    background: #7c3aed;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    width: 14px;
    margin: -5px 0px;
    background: #8b5cf6;
    border-radius: 7px;
}
QSlider::groove:vertical {
    width: 4px;
    background: #2a2338;
    border-radius: 2px;
}
QSlider::handle:vertical {
    height: 14px;
    margin: 0px -5px;
    background: #8b5cf6;
    border-radius: 7px;
}

/* Toolbars and status bar sit on the window tone */
QToolBar {
    background-color: #171420;
    border: none;
    spacing: 2px;
}
QToolBar::separator {
    background: #2c2540;
    width: 1px;
    margin: 4px 4px;
}
QToolButton {
    background: transparent;
    border: none;
    border-radius: 5px;
    padding: 4px;
}
QToolButton:hover {
    background-color: #2a2338;
}
QToolButton:pressed {
    background-color: #201a2e;
}
QToolButton:checked {
    background-color: rgba(124, 58, 237, 70);
}
QStatusBar {
    background-color: #171420;
    color: #a99fc0;
}
QStatusBar::item {
    border: none;
}

/* Splitters and docks */
QSplitter::handle {
    background: transparent;
}
QSplitter::handle:hover {
    background: #322a48;
}
QDockWidget::title {
    background-color: #1d1830;
    padding: 5px 8px;
    text-align: left;
    color: #b8aed0;
}
)qss");
}
