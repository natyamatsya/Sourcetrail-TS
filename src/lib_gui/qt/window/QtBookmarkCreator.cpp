#include "QtBookmarkCreator.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QVBoxLayout>

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
#endif

QtBookmarkCreator::QtBookmarkCreator(ControllerProxy<BookmarkController>* controllerProxy, QWidget* parent, BookmarkId bookmarkId)
	: QtBookmarkWindow(controllerProxy, false, parent)
	, m_editBookmarkId(bookmarkId)
{
	QVBoxLayout* mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);

	{
		// title
		QLabel* title = new QLabel(m_editBookmarkId != BookmarkId::NONE ? tr("Edit Bookmark") : tr("Create Bookmark"));
		title->setObjectName(QStringLiteral("creator_title_label"));
		mainLayout->addWidget(title);
	}

	QVBoxLayout* layout = new QVBoxLayout();
	layout->setContentsMargins(20, 10, 20, 20);

	{
		// name
		QLabel* nameLabel = new QLabel(tr("Name"));
		nameLabel->setObjectName(QStringLiteral("creator_label"));
		layout->addWidget(nameLabel);

		m_displayName = new QLineEdit();
		m_displayName->setObjectName(QStringLiteral("creator_name_edit"));
		m_displayName->setPlaceholderText(tr("Name"));
		m_displayName->setAttribute(Qt::WA_MacShowFocusRect, false);
		layout->addWidget(m_displayName);

		connect(m_displayName, &QLineEdit::textChanged, this, &QtBookmarkCreator::onNameChanged);
		connect(m_displayName, &QLineEdit::textChanged, this, &QtBookmarkCreator::onFieldsChanged);

		layout->addSpacing(15);
	}

	{
		// comment
		QLabel* commentLabel = new QLabel(tr("Comment"));
		commentLabel->setObjectName(QStringLiteral("creator_label"));
		layout->addWidget(commentLabel);

		m_commentBox = new QTextEdit();
		m_commentBox->setObjectName(QStringLiteral("creator_comment_box"));
		m_commentBox->setPlaceholderText(tr("Comment"));
		layout->addWidget(m_commentBox);

		connect(m_commentBox, &QTextEdit::textChanged, this, &QtBookmarkCreator::onFieldsChanged);

		layout->addSpacing(15);
	}

	{
		// category
		QLabel* categoryLabel = new QLabel(tr("Choose or Create Category"));
		categoryLabel->setObjectName(QStringLiteral("creator_label"));
		layout->addWidget(categoryLabel);

		m_categoryBox = new QComboBox();
		m_categoryBox->setObjectName(QStringLiteral("creator_category_box"));
		m_categoryBox->addItem(QLatin1String(""));
		m_categoryBox->setEditable(true);
		m_categoryBox->lineEdit()->setPlaceholderText(tr("Category"));
		m_categoryBox->setInsertPolicy(QComboBox::InsertPolicy::InsertAtTop);
		layout->addWidget(m_categoryBox);

		connect(m_categoryBox, &QComboBox::currentTextChanged, this, &QtBookmarkCreator::onFieldsChanged);

		layout->addSpacing(20);
	}

	{
		layout->addLayout(createButtons());
		setPreviousVisible(false);

		updateNextButton(m_editBookmarkId != BookmarkId::NONE ? tr("Save") : tr("Create"));
		setNextDefault(true);
	}

	mainLayout->addLayout(layout);

	refreshStyleSheet();
}

void QtBookmarkCreator::setDisplayName(const std::string& name)
{
	m_displayName->setText(QString::fromStdString(name));
}

void QtBookmarkCreator::setComment(const std::string& comment)
{
	m_commentBox->setText(QString::fromStdString(comment));
}

void QtBookmarkCreator::setBookmarkCategories(const std::vector<BookmarkCategory>& categories)
{
	for (unsigned int i = 0; i < categories.size(); i++)
	{
		m_categoryBox->addItem(QString::fromStdString(categories[i].getName()));
	}
}

void QtBookmarkCreator::setCurrentBookmarkCategory(const BookmarkCategory& category)
{
	int index = m_categoryBox->findText(QString::fromStdString(category.getName()));

	if (index > -1)
	{
		m_categoryBox->setCurrentIndex(index);
	}
	else
	{
		m_categoryBox->addItem(QString::fromStdString(category.getName()));
		m_categoryBox->setCurrentIndex(1);
	}
}

void QtBookmarkCreator::setCommit(std::function<void(const BookmarkFields&)> commit)
{
	m_commit = std::move(commit);
}

void QtBookmarkCreator::setEditorId(std::uint64_t editorId)
{
	m_editorId = editorId;
}

void QtBookmarkCreator::showFields(const BookmarkFields& fields)
{
	m_displayName->setText(QString::fromStdString(fields.name));
	m_commentBox->setPlainText(QString::fromStdString(fields.comment));
	m_categoryBox->setCurrentText(QString::fromStdString(fields.category));
}

BookmarkFields QtBookmarkCreator::fields() const
{
	return {
		m_displayName->text().toStdString(),
		m_commentBox->toPlainText().toStdString(),
		m_categoryBox->currentText().toStdString()};
}

void QtBookmarkCreator::handleNext()
{
	// Saved through the bookmark machine, as an agent saves it; should the machine
	// be elsewhere (it never is while this editor shows), the click still counts.
	if (!BookmarkStateMachine::instance()->save(fields(), EventSource::Ui, m_editorId).ok && m_commit)
	{
		m_commit(fields());
	}
	m_commit = nullptr;
	close();
}

void QtBookmarkCreator::handleClose()
{
	BookmarkStateMachine::instance()->cancel(EventSource::Ui, m_editorId);
	close();
}

void QtBookmarkCreator::onFieldsChanged()
{
	BookmarkStateMachine::instance()->edited(m_editorId, fields());
}

void QtBookmarkCreator::onNameChanged(const QString& text)
{
	setNextEnabled(!text.trimmed().isEmpty());
}
