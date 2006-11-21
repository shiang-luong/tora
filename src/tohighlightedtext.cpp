/*****
*
* TOra - An Oracle Toolkit for DBA's and developers
* Copyright (C) 2003-2005 Quest Software, Inc
* Portions Copyright (C) 2005 Other Contributors
* 
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation;  only version 2 of
* the License is valid for this program.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*
*      As a special exception, you have permission to link this program
*      with the Oracle Client libraries and distribute executables, as long
*      as you follow the requirements of the GNU GPL in regard to all of the
*      software in the executable aside from Oracle client libraries.
*
*      Specifically you are not permitted to link this program with the
*      Qt/UNIX, Qt/Windows or Qt Non Commercial products of TrollTech.
*      And you are not permitted to distribute binaries compiled against
*      these libraries without written consent from Quest Software, Inc.
*      Observe that this does not disallow linking to the Qt Free Edition.
*
*      You may link this product with any GPL'd Qt library such as Qt/Free
*
* All trademarks belong to their respective owners.
*
*****/

#include "utils.h"

#include "toconf.h"
#include "toconnection.h"
#include "tohighlightedtext.h"
#include "tosqlparse.h"
#include "totool.h"

#include <ctype.h>


#include <qapplication.h>
#include <qpainter.h>
#include <qpalette.h>
#include <qsimplerichtext.h>
#include <qstylesheet.h>
#include <qtimer.h>
#include <qkeysequence.h>
#include <qextscintillaapis.h>
#include <qnamespace.h>

#include <qextscintillalexersql.h>
#include <qpoint.h>

#include "todefaultkeywords.h"

#include "tohighlightedtext.moc"

// Default SQL lexer
static QextScintillaLexerSQL sqlLexer(0);

toSyntaxAnalyzer::toSyntaxAnalyzer(const char **keywords)
{
    for (int i = 0;keywords[i];i++)
    {
        std::list<const char *> &curKey = Keywords[(unsigned char)char(toupper(*keywords[i]))];
        curKey.insert(curKey.end(), keywords[i]);
    }
    ColorsUpdated = false;
}

toSyntaxAnalyzer::posibleHit::posibleHit(const char *text)
{
    Pos = 1;
    Text = text;
}

QColor toSyntaxAnalyzer::getColor(toSyntaxAnalyzer::infoType typ)
{
    if (!ColorsUpdated)
    {
        updateSettings();
        ColorsUpdated = true;
    }
    return Colors[typ];
}

#define ISIDENT(c) (isalnum(c)||(c)=='_'||(c)=='%'||(c)=='$'||(c)=='#')

std::list<toSyntaxAnalyzer::highlightInfo> toSyntaxAnalyzer::analyzeLine(const QString &str,
        toSyntaxAnalyzer::infoType in,
        toSyntaxAnalyzer::infoType &out)
{
    std::list<highlightInfo> highs;
    std::list<posibleHit> search;

    bool inWord;
    bool wasWord = false;
    int multiComment = -1;
    int inString = -1;
    QChar endString;

    if (in == String)
    {
        inString = 0;
        endString = '\'';
    }
    else if (in == Comment)
    {
        multiComment = 0;
    }

    char c;
    char nc = str[0].latin1();
    for (int i = 0;i < int(str.length());i++)
    {
        std::list<posibleHit>::iterator j = search.begin();

        c = nc;
        if (int(str.length()) > i)
            nc = str[i + 1].latin1();
        else
            nc = ' ';

        bool nextSymbol = ISIDENT(nc);
        if (multiComment >= 0)
        {
            if (c == '*' && nc == '/')
            {
                highs.insert(highs.end(), highlightInfo(multiComment, Comment));
                highs.insert(highs.end(), highlightInfo(i + 2));
                multiComment = -1;
            }
        }
        else if (inString >= 0)
        {
            if (c == endString)
            {
                highs.insert(highs.end(), highlightInfo(inString, String));
                highs.insert(highs.end(), highlightInfo(i + 1));
                inString = -1;
            }
        }
        else if (c == '\'' || c == '\"')
        {
            inString = i;
            endString = str[i];
            search.clear();
            wasWord = false;
        }
        else if (c == '-' && nc == '-')
        {
            highs.insert(highs.end(), highlightInfo(i, Comment));
            highs.insert(highs.end(), highlightInfo(str.length() + 1));
            out = Normal;
            return highs;
        }
        else if (c == '/' && nc == '/')
        {
            highs.insert(highs.end(), highlightInfo(i, Comment));
            highs.insert(highs.end(), highlightInfo(str.length() + 1));
            out = Normal;
            return highs;
        }
        else if (c == '/' && nc == '*')
        {
            multiComment = i;
            search.clear();
            wasWord = false;
        }
        else
        {
            std::list<posibleHit> newHits;
            while (j != search.end())
            {
                posibleHit &cur = (*j);
                if (cur.Text[cur.Pos] == toupper(c))
                {
                    cur.Pos++;
                    if (!cur.Text[cur.Pos] && !nextSymbol)
                    {
                        newHits.clear();
                        highs.insert(highs.end(), highlightInfo(i - cur.Pos, Keyword));
                        highs.insert(highs.end(), highlightInfo(i + 1));
                        break;
                    }
                    newHits.insert(newHits.end(), cur);
                }
                j++;
            }
            search = newHits;
            if (ISIDENT(c))
                inWord = true;
            else
                inWord = false;

            if (!wasWord && inWord)
            {
                std::list<const char *> &curKey = Keywords[(unsigned char)char(toupper(c))];
                for (std::list<const char *>::iterator j = curKey.begin();
                        j != curKey.end();j++)
                {
                    if (strlen(*j) == 1)
                    {
                        if (!nextSymbol)
                        {
                            highs.insert(highs.end(), highlightInfo(i, Keyword));
                            highs.insert(highs.end(), highlightInfo(i));
                        }
                    }
                    else
                        search.insert(search.end(), posibleHit(*j));
                }
            }
            wasWord = inWord;
        }
    }
    if (inString >= 0)
    {
        if (endString == '\'')
        {
            out = String;
            highs.insert(highs.end(), highlightInfo(inString, String));
        }
        else
        {
            out = Normal;
            highs.insert(highs.end(), highlightInfo(inString, Error));
        }
        highs.insert(highs.end(), highlightInfo(str.length() + 1));
    }
    else if (multiComment >= 0)
    {
        highs.insert(highs.end(), highlightInfo(multiComment, Comment));
        highs.insert(highs.end(), highlightInfo(str.length() + 1));
        out = Comment;
    }
    else
        out = Normal;

    return highs;
}

static toSyntaxAnalyzer DefaultAnalyzer(DefaultKeywords);

toSyntaxAnalyzer &toSyntaxAnalyzer::defaultAnalyzer(void)
{
    return DefaultAnalyzer;
}

bool toSyntaxAnalyzer::reservedWord(const QString &str)
{
    if (str.length() == 0)
        return false;
    QString t = str.upper();
    std::list<const char *> &curKey = Keywords[(unsigned char)char(str[0].latin1())];
    for (std::list<const char *>::iterator i = curKey.begin();i != curKey.end();i++)
        if (t == (*i))
            return true;
    return false;
}

toComplPopup::toComplPopup(toHighlightedText* edit)
        :QListBox(0,"popup",Qt::WType_Popup|Qt::WStyle_NoBorder|Qt::WStyle_Customize){
  this->editor=edit;
}

toComplPopup::~toComplPopup(){
}


void toComplPopup::keyPressEvent(QKeyEvent * e){
  if ((e->text() && e->text().length()>0 && e->key()!=Qt::Key_Return
  && e->text()!=" ") || e->key()==Qt::Key_Backspace){
    this->editor->keyPressEvent(e);
    this->editor->autoCompleteFromAPIs();
  }else if (e->text() && e->text().length()>0 && e->text()==" "){
    this->editor->keyPressEvent(e);
    this->hide();
  }else
    QListBox::keyPressEvent(e);
  
}


toHighlightedText::toHighlightedText(QWidget *parent, const char *name)
        : toMarkedText(parent, name), lexer(0), syntaxColoring(false)
{
    sqlLexer.setDefaultFont(toStringToFont(toConfigurationSingle::Instance().globalConfig(CONF_CODE, "")));
    
    // set default SQL lexer (syntax colouring as well)
    setLexer (&sqlLexer);
    
    // enable line numbers
    setMarginLineNumbers (0, true);
    
    // enable syntax colouring
    setSyntaxColoring (true);
    
    // set the font
    setFont(toStringToFont(toConfigurationSingle::Instance().globalConfig(CONF_CODE, "")));

    errorMarker=markerDefine(Circle,4);
    setMarkerBackgroundColor(Qt::red,errorMarker);
    debugMarker=markerDefine(Rectangle,8);
    setMarkerBackgroundColor(Qt::darkGreen,debugMarker);
    setMarginMarkerMask(1,0);
    setAutoIndent(true);
    connect(this,SIGNAL(cursorPositionChanged(int,int)),this,SLOT(setStatusMessage(void )));
    complAPI=new QextScintillaAPIs();
    connect (this,SIGNAL(cursorPositionChanged(int,int)),this,SLOT(positionChanged(int,int)));
    timer=new QTimer(this);
    connect( timer, SIGNAL(timeout()), this, SLOT(autoCompleteFromAPIs()) );
    popup=new toComplPopup(this);
    popup->hide();
    connect(popup,SIGNAL(clicked(QListBoxItem*)),this,SLOT(completeFromAPI(QListBoxItem*)));
    connect(popup,SIGNAL(returnPressed(QListBoxItem*)),this,SLOT(completeFromAPI(QListBoxItem*)));
}

toHighlightedText::~toHighlightedText() 
{
  if(complAPI)
    delete complAPI;
  if(popup)
    delete popup;
}

void toHighlightedText::positionChanged(int row, int col){
  if (col>0 && this->text(row)[col-1]=='.'){
    timer->start(500,true);
  }else{
    if(timer->isActive())
      timer->stop();
  }
}

static QString UpperIdent(const QString &str){
  if (str.length() > 0 && str[0] == '\"')
    return str;
  else
    return str.upper();
}

void toHighlightedText::autoCompleteFromAPIs(){
  QString partial;
  QStringList compleList=this->getCompletionList(&partial);
  if(compleList.count()==1 && compleList.first()==partial){
    this->completeWithText(compleList.first());
    if(popup->isVisible())
      popup->hide();
  }else if(!compleList.isEmpty() || popup->isVisible()){
    long position, posx, posy;
    int curCol, curRow;
    this->getCursorPosition(&curRow,&curCol);
    position=this->SendScintilla(SCI_GETCURRENTPOS);
    posx=this->SendScintilla(SCI_POINTXFROMPOSITION,0,position);
    posy=this->SendScintilla(SCI_POINTYFROMPOSITION,0,position)+
      this->SendScintilla(SCI_TEXTHEIGHT,curRow);
    QPoint p(posx,posy);
    p=this->mapToGlobal(p);
    popup->move(p);
    popup->clear();
    popup->insertStringList(compleList);
    if(partial && partial.length()>0){
      int i;
      for(i=0;i<popup->numRows();i++){
        if(popup->item(i)->text().find(partial)==0){
          popup->setSelected(i,true);
          break;
        }
      }
 
    }
    popup->show();
  }else{
    popup->hide();
  }
}

bool toHighlightedText::invalidToken(int line, int col)
{
  bool ident = true;
  if (line < 0){
    line = 0;
    col = 0;
  }
  while (line < lines()){
    QString cl = text(line);
    while (col < int(cl.length())){
      QChar c = cl[col];
      if (!toIsIdent(c))
        ident = false;
      if (!ident && !c.isSpace())
        return c == '.';
        col++;
    }
    line++;
    col = 0;
  }
  return false;
}

/** 
 * Sets the syntax colouring flag.
 */
void toHighlightedText::setSyntaxColoring(bool val)
{
    syntaxColoring = val;
    if (syntaxColoring) {
        QextScintilla::setLexer(lexer);
        update();
    }
    else {
        QextScintilla::setLexer(0);
    }
}

/**
 * Set the lexer to use. 
 * @param lexer to use,
 *        0 if no syntax colouring
 */
void toHighlightedText::setLexer(QextScintillaLexer *lexer) 
{
    if (lexer != 0) {
        this->lexer = lexer;
    }
    // refresh scintilla lexer
    setSyntaxColoring(syntaxColoring);
}

void toHighlightedText::setFont (const QFont & font)
{
    // Only sets fint lexer - one for all styles
    // this may (or may not) need to be changed in a future
    if (lexer) {
        lexer->setDefaultFont(font);
        lexer->setFont(font);
        update();
    }
}
void toHighlightedText::setCurrent(int current)
    {
        setCursorPosition (current, 0);
        markerDeleteAll(debugMarker);
        if(current>=0)
          markerAdd(current,debugMarker);
    }
void toHighlightedText::tableAtCursor(QString &owner, QString &table, bool mark)
{
    try
    {
        toConnection &conn = toCurrentConnection(this);
        int curline, curcol;
        getCursorPosition (&curline, &curcol);

        QString token = text(curline);
        toSQLParse::editorTokenizer tokens(this, curcol, curline);
        if (curcol > 0 && toIsIdent(token[curcol - 1]))
            token = tokens.getToken(false);
        else
            token = QString::null;

        toSQLParse::editorTokenizer lastTokens(this, tokens.offset(), tokens.line());
        token = tokens.getToken(false);
        if (token == ".")
        {
            lastTokens.setLine(tokens.line());
            lastTokens.setOffset(tokens.offset());
            owner = conn.unQuote(tokens.getToken(false));
            lastTokens.getToken(true);
            table += conn.unQuote(lastTokens.getToken(true));
        }
        else
        {
            tokens.setLine(lastTokens.line());
            tokens.setOffset(lastTokens.offset());
            owner = conn.unQuote(lastTokens.getToken(true));
            int tmplastline = lastTokens.line();
            int tmplastcol = lastTokens.offset();
            token = lastTokens.getToken(true);
            if (token == ".")
                table = conn.unQuote(lastTokens.getToken(true));
            else
            {
                lastTokens.setLine(tmplastline);
                lastTokens.setOffset(tmplastcol);
                table = owner;
                owner = QString::null;
            }
        }
        if (mark)
        {
            if (lastTokens.line() >= lines())
            {
                lastTokens.setLine(lines() - 1);
                lastTokens.setOffset(text(lines() - 1).length());
            }
            setSelection(tokens.line(), tokens.offset(), lastTokens.line(), lastTokens.offset());
        }
    }
    catch (...)
    {}
}

 bool toHighlightedText::hasErrors(){
     if ( Errors.empty() )
         return (false); 
     else
         return (true);
}

void toHighlightedText::nextError(void){
    int curline, curcol;
    getCursorPosition (&curline, &curcol);
    for (std::map<int, QString>::iterator i = Errors.begin();i != Errors.end();i++){   
        if ((*i).first > curline){
            setCursorPosition((*i).first, 0);
            break;
        }
    }
}

void toHighlightedText::previousError(void){
    int curline, curcol;
    getCursorPosition (&curline, &curcol);
    curcol = -1;
    for (std::map<int, QString>::iterator i = Errors.begin();i != Errors.end();i++){
        if ((*i).first >= curline){
            if (curcol < 0)
                curcol = (*i).first;
            break;
        }
        curcol = (*i).first;
    }
    if (curcol >= 0)
        setCursorPosition(curcol, 0);
}

void toHighlightedText::setErrors(const std::map<int, QString> &errors)
{
    Errors = errors;
    setStatusMessage();
    markerDeleteAll(errorMarker);
    for (std::map<int, QString>::iterator i = Errors.begin();i != Errors.end();i++){
       markerAdd((*i).first,errorMarker);
    }
}

void toHighlightedText::setStatusMessage(void)
{
    int curline, curcol;
    getCursorPosition (&curline, &curcol);
    std::map<int, QString>::iterator err = Errors.find(curline);
    if (err == Errors.end())
        toStatusMessage(QString::null);
    else
        toStatusMessage((*err).second, true);
}

QStringList toHighlightedText::getCompletionList(QString* partial){
  int curline, curcol;
  QStringList toReturn;
  getCursorPosition (&curline, &curcol);

  QString line = text(curline);

  if (!isReadOnly() && curcol >= 0){
    if (toConfigurationSingle::Instance().globalConfig(CONF_CODE_COMPLETION, "Yes").isEmpty())
      return toReturn;
    
    toSQLParse::editorTokenizer tokens(this, curcol, curline);
    if (line[curcol-1]!='.'){
      *partial=tokens.getToken(false);
    }else{
      *partial="";
    } 

    QString name = tokens.getToken(false);
    QString owner;
    if (name == "."){
      name = tokens.getToken(false);
    }  

    QString token = tokens.getToken(false);
    if (token == ".")
      owner = tokens.getToken(false);
    else{
      QString cmp = UpperIdent(name);
      QString lastToken;
      while ((invalidToken(tokens.line(), tokens.offset() + token.length()) || UpperIdent(token) != cmp || lastToken == ".") && token != ";" && !token.isEmpty()){
        lastToken = token;
        token = tokens.getToken(false);
      }

      if(token == ";" || token.isEmpty()){
        tokens.setLine(curline);
        tokens.setOffset(curcol);
        token = tokens.getToken();
        while ((invalidToken(tokens.line(), tokens.offset()) || UpperIdent(token) != cmp && lastToken != ".") && token != ";" && !token.isEmpty())
          token = tokens.getToken();
        lastToken = token;
        tokens.getToken(false);
      }
      if(token != ";" && !token.isEmpty()){
        token = tokens.getToken(false);
        if (token != "TABLE" && token != "UPDATE" && token != "FROM" && token != "INTO" && (toIsIdent(token[0]) || token[0] == '\"')){
          name = token;
          token = tokens.getToken(false);
          if (token == ".")
            owner = tokens.getToken(false);
        }else if (token == ")"){
          return toReturn;
        }
      }
    }
    if (!owner.isEmpty()){
      name = owner + QString::fromLatin1(".") + name;
    }
    if (!name.isEmpty()){
      try{
        toConnection &conn = toCurrentConnection(this);
        toQDescList &desc = conn.columns(conn.realName(name, false));
        for (toQDescList::iterator i = desc.begin();i != desc.end();i++){
          QString t;
          int ind = (*i).Name.find("(");
          if (ind < 0)
            ind = (*i).Name.find("RETURNING") - 1; //it could be a function or procedure without parameters. -1 to remove the space
          if (ind >= 0)
            t = conn.quote((*i).Name.mid(0, ind)) + (*i).Name.mid(ind);
          else
            t = conn.quote((*i).Name);
          if (t.find(*partial)==0)
            toReturn.append(t);
        }
      }catch (...){}
    }
  }
  toReturn.sort();
  return toReturn;
}

void toHighlightedText::completeWithText(QString itemText){
  int curline, curcol, start,end;
  getCursorPosition (&curline, &curcol);
  QString line = text(curline);
  toSQLParse::editorTokenizer tokens(this, curcol, curline);
  if (line[curcol-1]!='.'){
    tokens.getToken(false);
    start=tokens.offset();
  }else{
    start=curcol;
  }
  if(line[curcol].isSpace()){
    end=curcol;
  }else{
    tokens.getToken(true);
    if(tokens.line()!=curline)
      end=line.length();
    else
      end=tokens.offset();
  }
  disconnect(this,SIGNAL(cursorPositionChanged(int,int)),this,SLOT(positionChanged(int,int)));
  setSelection(curline,start,curline,end);
  this->removeSelectedText();
  this->insert(itemText);
  this->setCursorPosition(curline,start+itemText.length());
  connect (this,SIGNAL(cursorPositionChanged(int,int)),this,SLOT(positionChanged(int,int)));
}
  

void toHighlightedText::completeFromAPI(QListBoxItem* item){
  if(item){
    this->completeWithText(item->text());
  }
  popup->hide();
}