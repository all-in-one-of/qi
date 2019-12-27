//##########################################################################
//#                                                                        #
//#                              CLOUDCOMPARE                              #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 or later of the License.      #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
//#                                                                        #
//##########################################################################

//GUIs generated by Qt Designer
#include <ui_openAsciiFileDlg.h>

//Local
#include "AsciiOpenDlg.h"
#include "FileIOFilter.h"

//qCC_db
#include <ccPointCloud.h>

//Qt
#include <QMessageBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QToolButton>
#include <QPushButton>
#include <QFile>
#include <QTextStream>

//system
#include <string.h>
#include <stdio.h>
#include <assert.h>

//Semi-persistent value for max. cloud size
static double s_maxCloudSizeDoubleSpinBoxValue = (double)CC_MAX_NUMBER_OF_POINTS_PER_CLOUD/1.0e6;

//! Dialog 'context'
struct AsciiOpenContext
{
	//! Default initializer
	AsciiOpenContext()
		: separator(' ')
		, extractSFNameFrom1stLine(false)
		, maxPointCountPerCloud(0)
		, skipLines(0)
		, applyAll(false)
	{}

	//! Saves state
	void save(Ui_AsciiOpenDialog* ui)
	{
		extractSFNameFrom1stLine = ui->extractSFNamesFrom1stLineCheckBox->isChecked();
		maxPointCountPerCloud = ui->maxCloudSizeDoubleSpinBox->value();
		separator = ui->lineEditSeparator->text()[0];
		skipLines = ui->spinBoxSkipLines->value();
	}

	//! Restores state
	void load(Ui_AsciiOpenDialog* ui) const
	{
		ui->extractSFNamesFrom1stLineCheckBox->setChecked(extractSFNameFrom1stLine);
		ui->maxCloudSizeDoubleSpinBox->setValue(maxPointCountPerCloud);
		ui->lineEditSeparator->blockSignals(true);
		ui->lineEditSeparator->setText(separator);
		ui->lineEditSeparator->blockSignals(false);
		ui->spinBoxSkipLines->blockSignals(true);
		ui->spinBoxSkipLines->setValue(skipLines);
		ui->spinBoxSkipLines->blockSignals(false);
	}

	AsciiOpenDlg::Sequence sequence;
	QChar separator;
	bool extractSFNameFrom1stLine;
	double maxPointCountPerCloud;
	int skipLines;
	bool applyAll;
};
//! Semi-persistent loading context
static AsciiOpenContext s_asciiOpenContext;

AsciiOpenDlg::AsciiOpenDlg(QWidget* parent)
	: QDialog(parent)
	//, Ui::AsciiOpenDialog()
	, m_ui(new Ui_AsciiOpenDialog)
	, m_skippedLines(0)
	, m_separator(' ')
	, m_averageLineSize(-1.0)
	//, m_filename()
	, m_columnsCount(0)
{
	m_ui->setupUi(this);

	//spinBoxSkipLines->setValue(0);
	m_ui->commentLinesSkippedLabel->hide();

	connect(m_ui->applyButton,			SIGNAL(clicked()),						this, SLOT(apply()));
	connect(m_ui->applyAllButton,		SIGNAL(clicked()),						this, SLOT(applyAll()));
	connect(m_ui->cancelButton,			SIGNAL(clicked()),						this, SLOT(reject()));
	connect(m_ui->lineEditSeparator,	SIGNAL(textChanged(const QString &)),	this, SLOT(onSeparatorChange(const QString &)));
	connect(m_ui->spinBoxSkipLines,		SIGNAL(valueChanged(int)),				this, SLOT(setSkippedLines(int)));

	//shortcut buttons
	connect(m_ui->toolButtonShortcutESP,		SIGNAL(clicked()), this, SLOT(shortcutButtonPressed()));
	connect(m_ui->toolButtonShortcutTAB,		SIGNAL(clicked()), this, SLOT(shortcutButtonPressed()));
	connect(m_ui->toolButtonShortcutComma,		SIGNAL(clicked()), this, SLOT(shortcutButtonPressed()));
	connect(m_ui->toolButtonShortcutDotcomma,	SIGNAL(clicked()), this, SLOT(shortcutButtonPressed()));

	m_ui->maxCloudSizeDoubleSpinBox->setMaximum(static_cast<double>(CC_MAX_NUMBER_OF_POINTS_PER_CLOUD)/1.0e6);
	m_ui->maxCloudSizeDoubleSpinBox->setValue(s_maxCloudSizeDoubleSpinBoxValue);
}

AsciiOpenDlg::~AsciiOpenDlg()
{
	if (m_ui)
		delete m_ui;
}

void AsciiOpenDlg::setFilename(QString filename)
{
	m_filename = filename;
	m_ui->lineEditFileName->setText(m_filename);

	autoFindBestSeparator();
}

void AsciiOpenDlg::autoFindBestSeparator()
{
	QList<QChar> separators;
	separators << QChar(' ');
	separators << QChar('\t');
	separators << QChar(',');
	separators << QChar(';');

	//We try all default separators...
	size_t maxValidColumnCount = 0;
	QChar bestSep = separators.front();
	for (QChar sep : separators)
	{
		m_ui->lineEditSeparator->setText(sep); //this calls 'updateTable'

		//...until we find one that gives us at least 3 valid colums
		size_t validColumnCount = 0;
		for (ColumnType type : m_columnType)
			if (type != TEXT)
				++validColumnCount;

		if (validColumnCount > 2)
		{
			return;
		}
		else if (validColumnCount > maxValidColumnCount)
		{
			maxValidColumnCount = validColumnCount;
			bestSep = sep;
		}
	}

	//if we are here, it means that we couldn't find a configuration
	//with 3 valid columns (we use the best guess in this case)
	m_ui->lineEditSeparator->setText(bestSep); //this calls 'updateTable'
}

void AsciiOpenDlg::setSkippedLines(int linesCount)
{
	if (linesCount < 0)
		return;

	m_skippedLines = static_cast<unsigned>(linesCount);

	updateTable();
}

static bool CouldBeX (const QString& colHeader) { return colHeader.startsWith(AsciiHeaderColumns::X().toUpper()); }
static bool CouldBeY (const QString& colHeader) { return colHeader.startsWith(AsciiHeaderColumns::Y().toUpper()); }
static bool CouldBeZ (const QString& colHeader) { return colHeader.startsWith(AsciiHeaderColumns::Z().toUpper()); }
static bool CouldBeRf(const QString& colHeader) { return colHeader == AsciiHeaderColumns::Rf().toUpper(); }
static bool CouldBeGf(const QString& colHeader) { return colHeader == AsciiHeaderColumns::Gf().toUpper(); }
static bool CouldBeBf(const QString& colHeader) { return colHeader == AsciiHeaderColumns::Bf().toUpper(); }
static bool CouldBeR (const QString& colHeader) { return colHeader == AsciiHeaderColumns::R().toUpper() || colHeader.contains("RED"); }
static bool CouldBeG (const QString& colHeader) { return colHeader == AsciiHeaderColumns::G().toUpper() || colHeader.contains("GREEN"); }
static bool CouldBeB (const QString& colHeader) { return colHeader == AsciiHeaderColumns::B().toUpper() || colHeader.contains("BLUE"); }
static bool CouldBeNx(const QString& colHeader) { return colHeader.startsWith(AsciiHeaderColumns::Nx().toUpper()) || (colHeader.contains("NORM") && colHeader.contains("X")); }
static bool CouldBeNy(const QString& colHeader) { return colHeader.startsWith(AsciiHeaderColumns::Ny().toUpper()) || (colHeader.contains("NORM") && colHeader.contains("Y")); }
static bool CouldBeNz(const QString& colHeader) { return colHeader.startsWith(AsciiHeaderColumns::Nz().toUpper()) || (colHeader.contains("NORM") && colHeader.contains("Z")); }

static bool CouldBeGrey(const QString& colHeader) { return colHeader == AsciiHeaderColumns::Grey().toUpper(); }
static bool CouldBeRGBi(const QString& colHeader) { return colHeader == AsciiHeaderColumns::RGB32i().toUpper(); }
static bool CouldBeRGBf(const QString& colHeader) { return colHeader == AsciiHeaderColumns::RGB32f().toUpper(); }
static bool CouldBeScal(const QString& colHeader) { return colHeader.contains("SCALAR"); }

static const unsigned MAX_COLUMNS = 256;				//maximum number of columns that can be handled
static const unsigned LINES_READ_FOR_STATS = 200;		//number of lines read for stats
static const unsigned DISPLAYED_LINES = 20;				//number of displayed lines

static unsigned X_BIT		= 1;
static unsigned Y_BIT		= 2;
static unsigned Z_BIT		= 4;
static unsigned XYZ_BITS	= X_BIT | Y_BIT | Z_BIT;

static int EnabledBits(unsigned bitField3)
{
	int count = 0;
	if (bitField3 & X_BIT)
		++count;
	if (bitField3 & Y_BIT)
		++count;
	if (bitField3 & Z_BIT)
		++count;

	return count;
}

void AsciiOpenDlg::onSeparatorChange(const QString& separator)
{
	assert(separator.size() == 1);
	if (separator.length() < 1)
	{
		m_ui->asciiCodeLabel->setText("Enter a valid character!");
		m_ui->buttonFrame->setEnabled(false);
		m_ui->tableWidget->clear();
		m_columnType.clear();
		return;
	}

	//new separator
	m_separator = separator[0];
	m_ui->asciiCodeLabel->setText(QString("(ASCII code: %1)").arg(m_separator.unicode()));

	m_headerLine.clear(); //to force re-assignation of columns!
	m_columnType.clear();

	updateTable();
}

void AsciiOpenDlg::updateTable()
{
	m_ui->tableWidget->setEnabled(false);
	m_ui->extractSFNamesFrom1stLineCheckBox->setEnabled(false);

	bool hadValidHeader = !m_headerLine.isEmpty();
	m_headerLine.clear();

	if (m_filename.isEmpty())
	{
		m_ui->tableWidget->clear();
		return;
	}
	//we open the file in ASCII mode
	QFile file(m_filename);
	if (!file.open(QFile::ReadOnly))
	{
		m_ui->tableWidget->clear();
		m_columnType.clear();
		return;
	}
	QTextStream stream(&file);

	//we skip first lines (if needed)
	{
		for (unsigned i = 0; i < m_skippedLines; ++i)
		{
			QString currentLine = stream.readLine();
			//we keep track of the first line
			if (i == 0)
				m_headerLine = currentLine;
		}
	}

	//if the old setup has less than 3 columns, we forget it
	if (m_columnsCount < 3)
	{
		m_ui->tableWidget->clear();
		m_columnType.clear();
		m_columnsCount = 0;
	}
	m_ui->tableWidget->setRowCount(DISPLAYED_LINES + 1);	//+1 for first line shifting

	unsigned lineCount = 0;			//number of lines read
	unsigned totalChars = 0;		//total read characters (for stats)
	unsigned columnsCount = 0;		//max columns count per line
	unsigned commentLines = 0;		//number of comments line skipped

	std::vector<bool> valueIsNumber;	//identifies columns with numbers only [mandatory]
	std::vector<bool> valueIsBelowOne;	//identifies columns with values between -1 and 1 only
	std::vector<bool> valueIsInteger;	//identifies columns with integer values only
	std::vector<bool> valueIsBelow255;	//identifies columns with integer values between 0 and 255 only

	QChar decimalPoint = QLocale().decimalPoint();
	QString currentLine = stream.readLine();
	while (lineCount < LINES_READ_FOR_STATS && !currentLine.isNull())
	{
		//we recognize "//" as the beginning of a comment
		if (!currentLine.startsWith("//")/* || !currentLine.startsWith("#")*/)
		{
			QStringList parts = currentLine.trimmed().split(m_separator, QString::SkipEmptyParts);
			if (lineCount < DISPLAYED_LINES)
			{
				unsigned partsCount = std::min(MAX_COLUMNS, static_cast<unsigned>(parts.size()));
				bool columnCountHasIncreased = (partsCount > columnsCount);

				//do we need to add one or several new columns?
				if (columnCountHasIncreased)
				{
					//we also extend vectors
					for (unsigned i = columnsCount; i < partsCount; ++i)
					{
						valueIsNumber.push_back(true);
						valueIsBelowOne.push_back(true);
						valueIsBelow255.push_back(true);
						valueIsInteger.push_back(true);
					}

					if (m_ui->tableWidget->columnCount() < static_cast<int>(partsCount))
					{
						//DGM: at this stage we must not reduce the table!
						//The first line is sometimes smaller than the next ones
						//and we want to keep the widgets/configuration for the
						//other columns!
						m_ui->tableWidget->setColumnCount(partsCount);
					}
					else if (m_ui->tableWidget->columnCount() > static_cast<int>(partsCount))
					{
						//remove the unnecessary cells!
						for (int i = static_cast<int>(partsCount); i < m_ui->tableWidget->columnCount(); ++i)
						{
							m_ui->tableWidget->setItem(lineCount + 1, i, 0);
						}
					}
					columnsCount = partsCount;
				}

				//we fill the current row with extracted parts
				for (unsigned i = 0; i < partsCount; ++i)
				{
					QTableWidgetItem* newItem = new QTableWidgetItem(parts[i]);

					//test values
					bool isANumber = false;
					double value = parts[i].toDouble(&isANumber);
					if (!isANumber)
					{
						valueIsNumber[i] = false;
						valueIsBelowOne[i] = false;
						valueIsInteger[i] = false;
						valueIsBelow255[i] = false;
						newItem->setBackground(QBrush(QColor(255, 160, 160)));
					}
					else
					{
						if (columnCountHasIncreased || (lineCount == 1 && !valueIsNumber[i]))
						{
							//the previous lines were probably header lines
							//we can forget about their content otherwise it will prevent us from detecting the right pattern
							valueIsNumber[i] = true;
							valueIsBelowOne[i] = true;
							valueIsInteger[i] = true;
							valueIsBelow255[i] = true;
						}
						valueIsBelowOne[i] = valueIsBelowOne[i] && (fabs(value) <= 1.0);
						valueIsInteger[i] = valueIsInteger[i] && !parts[i].contains(decimalPoint);
						valueIsBelow255[i] = valueIsBelow255[i] && valueIsInteger[i] && (value >= 0.0 && value <= 255.0);
					}

					m_ui->tableWidget->setItem(lineCount + 1, i, newItem); //+1 for first line shifting
				}
			}

			totalChars += currentLine.size() + 1; //+1 for return char at eol
			++lineCount;
		}
		else
		{
			if (m_skippedLines == 0 && commentLines == 0)
			{
				//if the very first line is a comment, then we force the user to skip it!
				//this way it will be considered as a header
				m_ui->spinBoxSkipLines->setMinimum(1);
				return;
			}
			++commentLines;
		}

		//read next line
		currentLine = stream.readLine();
	}

	file.close();

	//now we can reduce the table (if necessary)
	if (m_ui->tableWidget->columnCount() > static_cast<int>(columnsCount))
	{
		m_ui->tableWidget->setColumnCount(columnsCount);
	}

	//process header line
	if (!m_headerLine.isEmpty())
	{
		m_headerLine = m_headerLine.trimmed();
		int n = 0;
		while (n < m_headerLine.size() && m_headerLine.at(n) == '/')
		{
			++n;
		}
		if (n != 0)
		{
			m_headerLine.remove(0, n);
		}
		m_ui->headerLabel->setText(QString("Header: ") + m_headerLine);
		m_ui->headerLabel->setVisible(true);
	}
	else
	{
		m_ui->headerLabel->setVisible(false);
	}

	m_ui->commentLinesSkippedLabel->setVisible(commentLines != 0);
	if (commentLines)
	{
		m_ui->commentLinesSkippedLabel->setText(QString("+ %1 comment line(s) skipped").arg(commentLines));
	}

	if (lineCount == 0 || columnsCount == 0)
	{
		m_averageLineSize = -1.0;
		m_ui->tableWidget->clear();
		m_columnType.clear();
		return;
	}

	//average line size
	m_averageLineSize = static_cast<double>(totalChars) / lineCount;

	//we add a type selector for each column
	QStringList propsText;
	{
		for (unsigned i = 0; i < ASCII_OPEN_DLG_TYPES_NUMBER; i++)
		{
			propsText << QString(ASCII_OPEN_DLG_TYPES_NAMES[i]);
		}
	}

	//remove unnecessary columns
	{
		while (columnsCount < m_columnsCount)
			m_ui->tableWidget->removeColumn(--m_columnsCount);
		if (m_columnType.size() > columnsCount)
			m_columnType.resize(columnsCount, UNKNOWN);
		for (unsigned i = lineCount + 1; i <= DISPLAYED_LINES; ++i)
			m_ui->tableWidget->removeRow(i);
	}

	//setup table and widgets
	{
		//Icons
		static const QIcon xIcon		(QString::fromUtf8(":/CC/images/typeXCoordinate.png"));
		static const QIcon yIcon		(QString::fromUtf8(":/CC/images/typeYCoordinate.png"));
		static const QIcon zIcon		(QString::fromUtf8(":/CC/images/typeZCoordinate.png"));
		static const QIcon NormIcon		(QString::fromUtf8(":/CC/images/typeNormal.png"));
		static const QIcon RGBIcon		(QString::fromUtf8(":/CC/images/typeRgbCcolor.png"));
		static const QIcon GreyIcon		(QString::fromUtf8(":/CC/images/typeGrayColor.png"));
		static const QIcon ScalarIcon	(QString::fromUtf8(":/CC/images/typeSF.png"));

		int columnWidth = (m_ui->tableWidget->width() * 9) / (columnsCount * 10);
		columnWidth = std::max(columnWidth, 80);

		for (unsigned i = 0; i < columnsCount; i++)
		{
			QComboBox* columnHeaderWidget = static_cast<QComboBox*>(m_ui->tableWidget->cellWidget(0, i));
			QComboBox* _columnHeader = columnHeaderWidget;
			if (!columnHeaderWidget)
			{
				columnHeaderWidget = new QComboBox();
				columnHeaderWidget->addItems(propsText);
				columnHeaderWidget->setMaxVisibleItems(ASCII_OPEN_DLG_TYPES_NUMBER);
				columnHeaderWidget->setCurrentIndex(0);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_X, xIcon);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_Y, yIcon);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_Z, zIcon);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_NX, NormIcon);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_NY, NormIcon);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_NZ, NormIcon);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_R, RGBIcon);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_G, RGBIcon);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_B, RGBIcon);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_Rf, RGBIcon);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_Gf, RGBIcon);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_Bf, RGBIcon);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_Grey, GreyIcon);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_Scalar, ScalarIcon);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_RGB32i, RGBIcon);
				columnHeaderWidget->setItemIcon(ASCII_OPEN_DLG_RGB32f, RGBIcon);

				connect(columnHeaderWidget, SIGNAL(currentIndexChanged(int)), this, SLOT(columnsTypeHasChanged(int)));
			}

			while (m_columnType.size() <= static_cast<size_t>(i))
				m_columnType.push_back(UNKNOWN);
			assert(m_columnType.size() >= static_cast<size_t>(i));

			if (!_columnHeader)
				m_ui->tableWidget->setCellWidget(0, i, columnHeaderWidget);
			m_ui->tableWidget->setColumnWidth(i, columnWidth);

			//a non-numerical column can't be valid
			if (!valueIsNumber[i])
				m_columnType[i] = TEXT;
		}
	}

	//auto-detect columns 'roles'
	{
		//DGM: bit flags now
		unsigned assignedXYZFlags = 0;
		unsigned assignedNormFlags = 0;
		unsigned assignedRGBFlags = 0;

		//split header (if any)
		QStringList headerParts = m_headerLine.split(m_separator, QString::SkipEmptyParts);
		bool validHeader = (headerParts.size() >= static_cast<int>(columnsCount));
		m_ui->extractSFNamesFrom1stLineCheckBox->setEnabled(validHeader); //can we consider the first ignored line as a header?
		if (!validHeader)
		{
			//no need to keep it (+ it will serve as flag later)
			m_headerLine.clear();
		}
		else if (!hadValidHeader)
		{
			m_ui->extractSFNamesFrom1stLineCheckBox->setChecked(true);
			for (ColumnType& type : m_columnType)
			{
				if (type != TEXT)
					type = UNKNOWN;
			}
			//std::fill(m_columnType.begin(), m_columnType.end(), UNKNOWN); //if the header has changed, we force update of all columns!
		}

		//first with the help of the header (if any)
		if (validHeader)
		{
			for (unsigned i = 0; i < columnsCount; i++)
			{
				//we try to guess columns if we have a valid header for the first time!
				if (m_columnType[i] == UNKNOWN || m_columnType[i] == IGNORED)
				{
					QComboBox* columnHeaderWidget = static_cast<QComboBox*>(m_ui->tableWidget->cellWidget(0, i));
					assert(columnHeaderWidget);

					columnHeaderWidget->blockSignals(true);

					QString colHeader = headerParts[i].toUpper();

					if ((assignedXYZFlags & X_BIT) == 0 && CouldBeX(colHeader))
					{
						//X
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_X);
						assignedXYZFlags |= X_BIT; //update bit field accordingly
						m_columnType[i] = VALID;
					}
					else if ((assignedXYZFlags & Y_BIT) == 0 && CouldBeY(colHeader))
					{
						//Y
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_Y);
						assignedXYZFlags |= Y_BIT; //update bit field accordingly
						m_columnType[i] = VALID;
					}
					else if ((assignedXYZFlags & Z_BIT) == 0 && CouldBeZ(colHeader))
					{
						//Z
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_Z);
						assignedXYZFlags |= Z_BIT; //update bit field accordingly
						m_columnType[i] = VALID;
					}
					else if ((assignedRGBFlags & X_BIT) == 0 && CouldBeRf(colHeader))
					{
						//Red
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_Rf);
						assignedRGBFlags |= X_BIT; //update bit field accordingly
						m_columnType[i] = VALID;
					}
					else if ((assignedRGBFlags & Y_BIT) == 0 && CouldBeGf(colHeader))
					{
						//Green
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_Gf);
						assignedRGBFlags |= Y_BIT; //update bit field accordingly
						m_columnType[i] = VALID;
					}
					else if ((assignedRGBFlags & Z_BIT) == 0 && CouldBeBf(colHeader))
					{
						//Blue
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_Bf);
						assignedRGBFlags |= Z_BIT; //update bit field accordingly
						m_columnType[i] = VALID;
					}
					else if ((assignedRGBFlags & X_BIT) == 0 && CouldBeR(colHeader))
					{
						//Red
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_R);
						assignedRGBFlags |= X_BIT; //update bit field accordingly
						m_columnType[i] = VALID;
					}
					else if ((assignedRGBFlags & Y_BIT) == 0 && CouldBeG(colHeader))
					{
						//Green
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_G);
						assignedRGBFlags |= Y_BIT; //update bit field accordingly
						m_columnType[i] = VALID;
					}
					else if ((assignedRGBFlags & Z_BIT) == 0 && CouldBeB(colHeader))
					{
						//Blue
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_B);
						assignedRGBFlags |= Z_BIT; //update bit field accordingly
						m_columnType[i] = VALID;
					}
					else if ((assignedNormFlags & X_BIT) == 0 && CouldBeNx(colHeader))
					{
						//Nx
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_NX);
						assignedNormFlags |= X_BIT; //update bit field accordingly
						m_columnType[i] = VALID;
					}
					else if ((assignedNormFlags & Y_BIT) == 0 && CouldBeNy(colHeader))
					{
						//Ny
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_NY);
						assignedNormFlags |= Y_BIT; //update bit field accordingly
						m_columnType[i] = VALID;
					}
					else if ((assignedNormFlags & Z_BIT) == 0 && CouldBeNz(colHeader))
					{
						//Nz
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_NZ);
						assignedNormFlags |= Z_BIT; //update bit field accordingly
						m_columnType[i] = VALID;
					}
					else if (CouldBeGrey(colHeader))
					{
						//Intensity
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_Grey);
						m_columnType[i] = VALID;
					}
					else if (CouldBeRGBi(colHeader))
					{
						//RGBi
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_RGB32i);
						m_columnType[i] = VALID;
					}
					else if (CouldBeRGBf(colHeader))
					{
						//RGBf
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_RGB32f);
						m_columnType[i] = VALID;
					}
					else if (CouldBeScal(colHeader))
					{
						//scalar field
						columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_Scalar);
						m_columnType[i] = VALID;
					}
				}
			}
		} //if (validHeader)

		//now for the auto-detection whithout header
		{
			for (unsigned i = 0; i < columnsCount; i++)
			{
				if (m_columnType[i] == TEXT)
				{
					continue;
				}
				assert(valueIsNumber[i]);

				//first time? let's try to assign each column a type
				if (m_columnType[i] == UNKNOWN && columnsCount > 1)
				{
					QComboBox* columnHeaderWidget = static_cast<QComboBox*>(m_ui->tableWidget->cellWidget(0, i));
					assert(columnHeaderWidget);
					columnHeaderWidget->blockSignals(true);
					columnHeaderWidget->setCurrentIndex(-1);

					//by default, we assume that the first columns are always X,Y and Z
					if (assignedXYZFlags < XYZ_BITS)
					{
						//in rare cases, the first column is an index
						if (columnsCount > 3
							&& i == 0
							&& (EnabledBits(assignedXYZFlags) == 0)
							&& valueIsInteger[i]
							&& i + 1 < columnsCount
							&& !valueIsInteger[i + 1])
						{
							//let's consider it as a scalar
							columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_Scalar);
						}
						else
						{
							if (!(assignedXYZFlags & X_BIT))
							{
								columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_X);
								assignedXYZFlags |= X_BIT; //update bit field accordingly
							}
							else if (!(assignedXYZFlags & Y_BIT))
							{
								columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_Y);
								assignedXYZFlags |= Y_BIT; //update bit field accordingly
							}
							else if (!(assignedXYZFlags & Z_BIT))
							{
								columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_Z);
								assignedXYZFlags |= Z_BIT; //update bit field accordingly
							}
						}
					}
					else
					{
						//looks like RGB?
						if (valueIsBelow255[i]
							&& assignedRGBFlags < XYZ_BITS
							&& (i + 2 - EnabledBits(assignedRGBFlags)) < columnsCount //make sure that we can put all values there!
							&& (EnabledBits(assignedRGBFlags) > 0 || (valueIsBelow255[i + 1] && valueIsBelow255[i + 2])) //make sure that next values are also ok!
							)
						{
							if (!(assignedRGBFlags & X_BIT))
							{
								columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_R);
								assignedRGBFlags |= X_BIT; //update bit field accordingly
							}
							else if (!(assignedRGBFlags & Y_BIT))
							{
								columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_G);
								assignedRGBFlags |= Y_BIT; //update bit field accordingly
							}
							else if (!(assignedRGBFlags & Z_BIT))
							{
								columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_B);
								assignedRGBFlags |= Z_BIT; //update bit field accordingly
							}
						}
						//looks like a normal vector?
						else if (valueIsBelowOne[i]
							&& assignedNormFlags < XYZ_BITS
							&& (i + 2 - EnabledBits(assignedNormFlags) < columnsCount) //make sure that we can put all values there!
							&& (EnabledBits(assignedNormFlags) > 0 || (valueIsBelowOne[i + 1] && valueIsBelowOne[i + 2])) //make sure that next values are also ok!
							) //make sure that next values are also ok!
						{
							if (!(assignedNormFlags & X_BIT))
							{
								columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_NX);
								assignedNormFlags |= X_BIT; //update bit field accordingly
							}
							else if (!(assignedNormFlags & Y_BIT))
							{
								columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_NY);
								assignedNormFlags |= Y_BIT; //update bit field accordingly
							}
							else if (!(assignedNormFlags & Z_BIT))
							{
								columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_NZ);
								assignedNormFlags |= Z_BIT; //update bit field accordingly
							}
						}
						else
						{
							//maybe it's a scalar?
							columnHeaderWidget->setCurrentIndex(ASCII_OPEN_DLG_Scalar);
						}
					}

					if (columnHeaderWidget->currentIndex() >= 0)
					{
						m_columnType[i] = VALID;
					}
					else
					{
						//we won't look at this column again
						m_columnType[i] = IGNORED;
					}

					columnHeaderWidget->blockSignals(false);
				}
				else
				{
					//we won't look at this column again
					m_columnType[i] = IGNORED;
				}
			}
		}
	}

	m_columnsCount = columnsCount;

	m_ui->tableWidget->setEnabled(true);
	m_ui->buttonFrame->setEnabled(true);

	//check for invalid columns
	checkSelectedColumnsValidity(); //will eventually enable of disable the "OK" button
	//expand dialog width to display all table columns
	resizeWidthToFitTableColumns();
}

void AsciiOpenDlg::checkSelectedColumnsValidity()
{
	//check for invalid columns
	bool m_selectedInvalidColumns = false;
	{
		assert(m_columnType.size() == static_cast<size_t>(m_columnsCount));
		assert(m_ui->tableWidget->columnCount() >= static_cast<int>(m_columnsCount));
		for (unsigned i = 0; i < m_columnsCount; i++)
		{
			QComboBox* columnHeaderWidget = static_cast<QComboBox*>(m_ui->tableWidget->cellWidget(0, i));
			m_selectedInvalidColumns |= (m_columnType[i] == TEXT && columnHeaderWidget->currentIndex() != 0);
		}
	}

	m_ui->applyAllButton->setEnabled(!m_selectedInvalidColumns);
	m_ui->applyButton->setEnabled(!m_selectedInvalidColumns);
}

bool AsciiOpenDlg::CheckOpenSequence(const AsciiOpenDlg::Sequence& sequence, QString& errorMessage)
{
	//two requirements:
	//- at least 2 coordinates must be defined
	//- apart from SFs, only one column assignment per property
	std::vector<unsigned> counters(ASCII_OPEN_DLG_TYPES_NUMBER,0);
	{
		for (size_t i=0; i<sequence.size(); i++)
			++counters[sequence[i].type];
	}

	//check for doublons
	{
		for (size_t i=1; i<ASCII_OPEN_DLG_Scalar; i++)
		{
			if (counters[i] > 1)
			{
				errorMessage = QString("'%1' defined at least twice!").arg(ASCII_OPEN_DLG_TYPES_NAMES[i]);
				return false;
			}
		}
	}

	unsigned char coordIsDefined[3] = {	counters[ASCII_OPEN_DLG_X] != 0,
										counters[ASCII_OPEN_DLG_Y] != 0,
										counters[ASCII_OPEN_DLG_Z] != 0 };

	if (coordIsDefined[0] + coordIsDefined[1] + coordIsDefined[2] < 2)
	{
		errorMessage = "At least 2 vertex coordinates must be defined!";
		return false;
	}

	return true;
}

bool AsciiOpenDlg::apply()
{
	QString errorMessage;
	if (!CheckOpenSequence(getOpenSequence(),errorMessage))
	{
		QMessageBox::warning(0, "Error", errorMessage);
		return false;
	}
	else
	{
		s_maxCloudSizeDoubleSpinBoxValue = m_ui->maxCloudSizeDoubleSpinBox->value();
		accept();
		return true;
	}
}

void AsciiOpenDlg::applyAll()
{
	if (!apply())
		return;

	//backup current open sequence
	s_asciiOpenContext.save(m_ui);
	s_asciiOpenContext.sequence = getOpenSequence();
	s_asciiOpenContext.applyAll = true;
}

bool AsciiOpenDlg::restorePreviousContext()
{
	if (!s_asciiOpenContext.applyAll)
		return false;

	//restore previous dialog state
	s_asciiOpenContext.load(m_ui);
	updateTable();

	//saved sequence and cloud content don't match!!!
	if (static_cast<size_t>(m_columnsCount) != s_asciiOpenContext.sequence.size())
	{
		s_asciiOpenContext.applyAll = false; //cancel the 'Apply All' effect
		return false;
	}

	//Restore columns attributes
	for (unsigned i = 0; i < m_columnsCount; i++)
	{
		QComboBox* combo = static_cast<QComboBox*>(m_ui->tableWidget->cellWidget(0, i));
		if (!combo) //yes, it happens if all lines are skipped!
		{
			s_asciiOpenContext.applyAll = false; //cancel the 'Apply All' effect
			return false;
		}
		SequenceItem& item = s_asciiOpenContext.sequence[i];
		combo->setCurrentIndex(item.type);
	}

	QString errorMessage;
	if (!CheckOpenSequence(s_asciiOpenContext.sequence, errorMessage))
	{
		s_asciiOpenContext.applyAll = false; //cancel the 'Apply All' effect
	}
	return s_asciiOpenContext.applyAll;
}

AsciiOpenDlg::Sequence AsciiOpenDlg::getOpenSequence() const
{
	Sequence seq;

	if (m_columnsCount != 0)
	{
		//shall we extract headerParts?
		QStringList headerParts;
		if (!m_headerLine.isEmpty()
			&& m_ui->extractSFNamesFrom1stLineCheckBox->isEnabled()
			&& m_ui->extractSFNamesFrom1stLineCheckBox->isChecked())
		{
			headerParts = m_headerLine.trimmed().split(m_separator, QString::SkipEmptyParts);
		}

		seq.reserve(m_columnsCount - 1);
		for (unsigned i = 0; i<m_columnsCount; i++)
		{
			const QComboBox* combo = static_cast<QComboBox*>(m_ui->tableWidget->cellWidget(0, i));
			if (!combo) //yes, it happens if all lines are skipped!
				break;
			seq.push_back(SequenceItem(static_cast<CC_ASCII_OPEN_DLG_TYPES>(combo->currentIndex()), headerParts.size() > static_cast<int>(i) ? headerParts[i] : QString()));
		}
	}

	return seq;
}

bool AsciiOpenDlg::safeSequence() const
{
	//less than 6 columns: we probably have a 3D point (3 columns)
	//and 2 other columns (i.e. scalar fields) --> no ambiguity
	if (getColumnsCount() < 6)
		return true;

	//no header
	if (m_headerLine.isEmpty())
		return false;

	AsciiOpenDlg::Sequence seq = getOpenSequence();
	QStringList headerParts = m_headerLine.split(m_separator,QString::SkipEmptyParts);

	//not enough column headers?
	if (headerParts.size() < static_cast<int>(seq.size()))
		return false;

	for (int i=0; i<headerParts.size(); ++i)
	{
		//column header
		QString colHeader = headerParts[i].toUpper();
		//column type
		switch (seq[i].type)
		{
		case ASCII_OPEN_DLG_None:
			break;
		case ASCII_OPEN_DLG_X:
			if (!CouldBeX(colHeader))
				return false;
			break;
		case ASCII_OPEN_DLG_Y:
			if (!CouldBeY(colHeader))
				return false;
			break;
		case ASCII_OPEN_DLG_Z:
			if (!CouldBeZ(colHeader))
				return false;
			break;
		case ASCII_OPEN_DLG_NX:
			if (!CouldBeNx(colHeader))
				return false;
			break;
		case ASCII_OPEN_DLG_NY:
			if (!CouldBeNy(colHeader))
				return false;
			break;
		case ASCII_OPEN_DLG_NZ:
			if (!CouldBeNz(colHeader))
				return false;
			break;
		case ASCII_OPEN_DLG_R:
		case ASCII_OPEN_DLG_Rf:
			if (!CouldBeR(colHeader))
				return false;
			break;
		case ASCII_OPEN_DLG_G:
		case ASCII_OPEN_DLG_Gf:
			if (!CouldBeG(colHeader))
				return false;
			break;
		case ASCII_OPEN_DLG_B:
		case ASCII_OPEN_DLG_Bf:
			if (!CouldBeB(colHeader))
				return false;
			break;
		case ASCII_OPEN_DLG_Grey:
			if (	!CouldBeGrey(colHeader)
				&&	!colHeader.contains("INT"))
				return false;
			break;
		case ASCII_OPEN_DLG_Scalar:
			//a SF name can be anything!
			break;
		case ASCII_OPEN_DLG_RGB32i:
			if (	!CouldBeRGBi(colHeader)
				&&	!colHeader.contains("RGB"))
				return false;
			break;
		case ASCII_OPEN_DLG_RGB32f:
			if (	!CouldBeRGBf(colHeader)
				&&	!colHeader.contains("RGB"))
				return false;
			break;
		default:
			//unhandled case?!
			assert(false);
			return false;
		}
	}

	return true;
}

void AsciiOpenDlg::columnsTypeHasChanged(int index)
{
	if (!m_columnsCount)
	{
		return;
	}

	//we get the signal sender
	QObject* obj = sender();
	if (!obj)
	{
		assert(false);
		return;
	}

	//it should be a QComboBox
	QComboBox* changedCombo = qobject_cast<QComboBox*>(obj);
	if (!changedCombo)
	{
		assert(false);
		return;
	}

	//now we look which column's combobox it is
	for (unsigned i = 0; i < m_columnsCount; i++)
	{
		QComboBox* combo = static_cast<QComboBox*>(m_ui->tableWidget->cellWidget(0,i));
		//we found the right element
		if (changedCombo == combo)
		{
			if (index == int(ASCII_OPEN_DLG_X)  ||
				index == int(ASCII_OPEN_DLG_NX) ||
				index == int(ASCII_OPEN_DLG_R)  ||
				index == int(ASCII_OPEN_DLG_Rf)
				)
			{
				//Auto select the next columns type
				if (i + 2 < m_columnsCount)
				{
					QComboBox* nextCombo = static_cast<QComboBox*>(m_ui->tableWidget->cellWidget(0, i + 1));
					QComboBox* nextNextCombo = static_cast<QComboBox*>(m_ui->tableWidget->cellWidget(0, i + 2));
					//if the two next columns have no assigned type, we set them auto.
					if (	nextCombo->currentIndex() == int(ASCII_OPEN_DLG_None)
						&&	nextNextCombo->currentIndex() == int(ASCII_OPEN_DLG_None))
					{
						nextCombo->blockSignals(true);
						nextNextCombo->blockSignals(true);

						if (index == int(ASCII_OPEN_DLG_X))
						{
							nextCombo->setCurrentIndex(ASCII_OPEN_DLG_Y);
							nextNextCombo->setCurrentIndex(ASCII_OPEN_DLG_Z);
						}
						else if (index == int(ASCII_OPEN_DLG_NX))
						{
							nextCombo->setCurrentIndex(ASCII_OPEN_DLG_NY);
							nextNextCombo->setCurrentIndex(ASCII_OPEN_DLG_NZ);
						}
						else if (index == int(ASCII_OPEN_DLG_R))
						{
							nextCombo->setCurrentIndex(ASCII_OPEN_DLG_G);
							nextNextCombo->setCurrentIndex(ASCII_OPEN_DLG_B);
						}
						else if (index == int(ASCII_OPEN_DLG_Rf))
						{
							nextCombo->setCurrentIndex(ASCII_OPEN_DLG_Gf);
							nextNextCombo->setCurrentIndex(ASCII_OPEN_DLG_Bf);
						}
					}

					nextCombo->blockSignals(false);
					nextNextCombo->blockSignals(false);
				}
			}
		}
		else if (index< ASCII_OPEN_DLG_Scalar) //check that the other combo as the same index (apart from SF)
		{
			if (combo->currentIndex() == index)
			{
				combo->blockSignals(true);
				combo->setCurrentIndex((int)ASCII_OPEN_DLG_None);
				combo->blockSignals(false);
			}
		}
	}

	checkSelectedColumnsValidity(); //will eventually enable of disable the "OK" button
}

void AsciiOpenDlg::shortcutButtonPressed()
{
	if (!m_columnsCount)
		return;

	//we get the signal sender
	QObject* obj = sender();
	if (!obj)
		return;

	//it should be a QToolButton (could we test this?)
	QToolButton* shortcutButton = static_cast<QToolButton*>(obj);

	char newSeparator = 0;
	if (shortcutButton == m_ui->toolButtonShortcutESP)
		newSeparator = 32;
	else if (shortcutButton == m_ui->toolButtonShortcutTAB)
		newSeparator = 9;
	else if (shortcutButton == m_ui->toolButtonShortcutComma)
		newSeparator = 44;
	else if (shortcutButton == m_ui->toolButtonShortcutDotcomma)
		newSeparator = 59;

	if (newSeparator>0 && getSeparator()!=newSeparator)
		m_ui->lineEditSeparator->setText(QChar(newSeparator));
}

unsigned AsciiOpenDlg::getMaxCloudSize() const
{
	return static_cast<unsigned>(floor(m_ui->maxCloudSizeDoubleSpinBox->value() * 1.0e6));
}

void AsciiOpenDlg::resizeWidthToFitTableColumns()
{
/*!
 * Increases dialog width if table widget contains many columns.
 * Increases table column widths to fill dialog if only a few
 * columns are present.
*/
    // First make sure all columns are wide enought to fit data
    // Then get combined width of all columns.
    m_ui->tableWidget->resizeColumnsToContents();
    int totalColumnsWidth = m_ui->tableWidget->horizontalHeader()->length();

    // To display whole table widget without h. scrollbars, need to add
    // table and layout margin pixel values to get a total required dialog width
    int leftTable, rightTable, leftLayout, rightLayout, top, bottom;
    m_ui->tableWidget->getContentsMargins(&leftTable, &top, &rightTable, &bottom);
    m_ui->verticalLayout->getContentsMargins(&leftLayout, &top, &rightLayout, &bottom);
    int totalMarginsWidth = leftTable + rightTable + leftLayout + rightLayout;
    int minDialogWidth = totalColumnsWidth + totalMarginsWidth;

    // If table requires more space, resize dialog
    if (minDialogWidth > this->width())
        this->resize(minDialogWidth, this->height());

    // Make columns stretchy and auto-resize
    // This is done differently in Qt5 vs. Qt4
#if QT_VERSION >= 0x050000
    m_ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
#else
    m_ui->tableWidget->horizontalHeader()->setResizeMode(QHeaderView::Stretch);
#endif

}
