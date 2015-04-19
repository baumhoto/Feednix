#include <curses.h>
#include <map>
#include <panel.h>
#include <menu.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <iterator>
#include <algorithm>
#include <string.h>
#include <cstring>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <json/json.h>

#include "CursesProvider.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define POSTS_STATUSLINE "Enter: See Preview  A: mark all read  u: mark unread  r: mark read  = : change sort type s: mark saved  S: mark unsaved R: refresh  o: Open in plain-text  O: Open in Browser  F1: exit"
#define CTG_STATUSLINE "Enter: Fetch Stream  A: mark all read  R: refresh  F1: exit"

extern std::string TMPDIR;
extern const std::string HOME_PATH;

CursesProvider::CursesProvider(bool verbose, bool change){
    feedly.setVerbose(verbose);
    feedly.setChangeTokensFlag(change);
    feedly.authenticateUser();

    // Set support for UTF-8 so long as libncursesw is used for compiliation
    setlocale(LC_ALL, "");
    initscr();

    start_color();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    lastEntryRead = "";
    currentCategoryRead = false;
    feedly.setVerbose(false);
}
void CursesProvider::init(){
    Json::Value root;
    Json::Reader reader;

    std::ifstream tokenFile(std::string(std::string(HOME_PATH) + "/.config/feednix/config.json").c_str(), std::ifstream::binary);
    if(reader.parse(tokenFile, root)){
        init_pair(1, root["colors"]["active_panel"].asInt(), root["colors"]["background"].asInt());
        init_pair(2, root["colors"]["idle_panel"].asInt(), root["colors"]["background"].asInt());
        init_pair(3, root["colors"]["counter"].asInt(), root["colors"]["background"].asInt());
        init_pair(4, root["colors"]["status_line"].asInt(), root["colors"]["background"].asInt());
        init_pair(5, root["colors"]["instructions_line"].asInt(), root["colors"]["background"].asInt());
        init_pair(6, root["colors"]["item_text"].asInt(), root["colors"]["background"].asInt());
        init_pair(7, root["colors"]["item_highlight"].asInt(), root["colors"]["background"].asInt());
        init_pair(8, root["colors"]["read_item"].asInt(), root["colors"]["background"].asInt());

        ctgWinWidth = root["ctg_win_width"].asInt();
        viewWinHeight = root["view_win_height"].asInt();
        viewWinHeightPer = root["view_win_height_per"].asInt();

        currentRank = root["rank"].asBool();
        if(root.isMember("previewActive"))
            activatePreview = access("/usr/bin/w3m", X_OK) ? false : root["previewActive"].asBool();
        if(root.isMember("enablePersistentCount"))
            activeCount = root["enablePersistentCount"].asBool();
        if(root.isMember("markReadWhileScrolling"))
            markReadWhileScroll = root["markReadWhileScrolling"].asBool();
    }
    else{
        endwin();
        feedly.curl_cleanup();
        std::cerr << "ERROR: Config file could not be read." << std::endl;
        std::cerr << reader.getFormatedErrorMessages() << std::endl;
        exit(EXIT_FAILURE);
    }

    // Ensure that all windows are properly aligned according to user discretion
    // and wether or not the preview window should be active
    if(activatePreview){
        if (ctgWinWidth == 0)
            ctgWinWidth = CTG_WIN_WIDTH;
        if (viewWinHeight == 0 && viewWinHeightPer == 0)
            viewWinHeightPer = VIEW_WIN_HEIGHT_PER;
        if (viewWinHeight == 0)
            viewWinHeight = (unsigned int)(((LINES - 2) * viewWinHeightPer) / 100);

        viewWin = newwin(viewWinHeight, COLS - 2, (LINES - 2 - viewWinHeight), 1);
        panels[2] = new_panel(viewWin);
    }
    else{
        viewWinHeight = 0;
    }

    createCategoriesMenu();
    createPostsMenu();

    panels[0] = new_panel(ctgWin);
    panels[1] = new_panel(postsWin);

    set_panel_userptr(panels[0], panels[1]);
    set_panel_userptr(panels[1], panels[0]);

    update_infoline(POSTS_STATUSLINE);

    top = panels[1];
    top_panel(top);

    update_panels();
    doupdate();
}
void CursesProvider::eventHandler(){
    int ch;
    if(totalPosts == 0){
        curMenu = ctgMenu;
    }
    else{
        curMenu = postsMenu;
        // Place cursor in the scope of postsMenu by moving one item up
        changeSelectedItem(curMenu, REQ_UP_ITEM);
    }

    ITEM* curItem = current_item(curMenu);

    while((ch = getch()) != KEY_F(1) && ch != 'q'){
        curItem = current_item(curMenu);
        switch(ch){
            case 10:
                if(activatePreview) wclear(viewWin);
                if(curMenu == ctgMenu){
                    top = (PANEL *)panel_userptr(top);

                    update_statusline("[Updating stream]", "", false);

                    refresh();
                    update_panels();

                    ctgMenuCallback(strdup(item_name(current_item(curMenu))));

                    top_panel(top);

                    if(currentCategoryRead){
                        curMenu = ctgMenu;
                    }
                    else{
                        curMenu = postsMenu;
                        update_infoline(POSTS_STATUSLINE);
                    }
                }
                break;
            case 9:{
                       if(curMenu == ctgMenu){
                           curMenu = postsMenu;

                           win_show(postsWin, strdup("Posts"), 1, true);
                           win_show(ctgWin, strdup("Categories"), 2, false);

                           update_infoline(POSTS_STATUSLINE);
                           refresh();
                       }
                       else{
                           curMenu = ctgMenu;
                           win_show(ctgWin, strdup("Categories"), 1, true);
                           win_show(postsWin, strdup("Posts"), 2, false);

                           update_infoline(CTG_STATUSLINE);

                           refresh();
                       }

                       top = (PANEL *)panel_userptr(top);
                       top_panel(top);
                       break;
                   }
            case '=':
                   if(activatePreview) wclear(viewWin);
                   update_statusline("[Updating stream]", "", false);
                   refresh();

                   currentRank = !currentRank;

                   ctgMenuCallback(strdup(item_name(current_item(ctgMenu))));
                   break;
            case KEY_DOWN:
                   changeSelectedItem(curMenu, REQ_DOWN_ITEM);
                   break;
            case KEY_UP:
                   changeSelectedItem(curMenu, REQ_UP_ITEM);
                   break;
            case 'j':
                   changeSelectedItem(curMenu, REQ_DOWN_ITEM);
                   break;
            case 'k':
                   changeSelectedItem(curMenu, REQ_UP_ITEM);
                   break;
            case 'u':{
                         if (!item_opts(curItem)) {
                             std::vector<std::string> *temp = new std::vector<std::string>;
                             temp->push_back(item_description(curItem));

                             update_statusline("[Marking post unread]", NULL, true);
                             refresh();

                             feedly.markPostWithAction("keepUnread", temp);

                             item_opts_on(curItem, O_SELECTABLE);
                             numRead--;
                             numUnread++;

                             update_statusline("", NULL, true);
                         }


                         break;
                     }
            case 'r':{
                         if(postsMenu != NULL && curItem != NULL && postsItems != NULL)
                             markItemRead(curItem);
                         break;
                     }
            case 's':{
                         // The markPostWithAction commonly takes a vector pointer.
                         // A vector of a length of one is a little bit of overhead
                         // but its lifespan is short so it is neglible
                         std::vector<std::string> *temp = new std::vector<std::string>;
                         temp->push_back(item_description(curItem));

                         update_statusline("[Marking post saved]", NULL, true);
                         refresh();

                         feedly.markPostWithAction("markAsSaved", temp);
                         update_statusline("", NULL, true);

                         delete temp;

                         break;
                     }
            case 'S':{
                         std::vector<std::string> *temp = new std::vector<std::string>;
                         temp->push_back(item_description(curItem));

                         update_statusline("[Marking post Unsaved]", NULL, true);
                         refresh();

                         feedly.markPostWithAction("markAsUnsaved", temp);
                         update_statusline("", NULL, true);

                         delete temp;

                         break;
                     }
            case 'R':{
                         if(activatePreview) wclear(viewWin);
                         update_statusline("[Updating stream]", "", false);
                         refresh();

                         ctgMenuCallback(strdup(item_name(current_item(ctgMenu))));
                         break;
                     }
            case 'o':
                     postsMenuCallback(curItem, false);
                     break;
            case 'O':
                     system(("xdg-open " + feedly.getSinglePostData(item_index(curItem))->originURL + " &> /dev/null &").c_str());
                     markItemRead(curItem);
                     break;

            case 'a':
                     newFeedDialog();
                     break;

            case 'A':{
                         if(activatePreview) wclear(viewWin);
                         update_statusline("[Marking category read]", "", true);
                         refresh();

                         feedly.markCategoriesRead(feedly.getStreamId(item_name(current_item(ctgMenu))), lastEntryRead);

                         ctgMenuCallback(strdup(item_name(current_item(ctgMenu))));
                         currentCategoryRead = true;
                         curMenu = ctgMenu;

                         update_statusline("", NULL, true);
                         break;
                     }
        }
        update_panels();
        doupdate();
    }
}
void CursesProvider::createCategoriesMenu(){
    int height, width;
    int n_choices, i = 2;
    const std::map<std::string, std::string> *labels = feedly.getLabels();

    n_choices = labels->size() + 1;
    if( ctgItems == NULL )
        ctgItems = (ITEM **)calloc(sizeof(std::string::value_type)*n_choices, sizeof(ITEM *));

    ctgItems[0] = new_item("All", "");
    ctgItems[1] = new_item("Saved", "");
    // We know what the type of begin() iterator should be. Lets use C++11's auto specifier instead
    // to make the code more readable.
    for(auto it = labels->begin(); it != labels->end(); ++it){
        if(it->first.compare("All") != 0 && it->first.compare("Saved") != 0){
            ctgItems[i] = new_item((it->first).c_str(), "");
            i++;

        }
    }

    ctgMenu = new_menu((ITEM **)ctgItems);

    ctgWin = newwin((LINES - 2 - viewWinHeight), ctgWinWidth, 0, 0);
    keypad(ctgWin, TRUE);

    getmaxyx(ctgWin, height, width);

    set_menu_win(ctgMenu, ctgWin);
    set_menu_sub(ctgMenu, derwin(ctgWin, height-4, width-2, 3, 1));
    set_menu_format(ctgMenu, height-4, 0);

    set_menu_fore(ctgMenu, COLOR_PAIR(7) | A_REVERSE);
    set_menu_back(ctgMenu, COLOR_PAIR(6));
    set_menu_grey(ctgMenu, COLOR_PAIR(8));

    set_menu_mark(ctgMenu, "  ");
    menu_opts_on(postsMenu, O_NONCYCLIC);
    post_menu(ctgMenu);

    if(activeCount) updateUnreadCount();

    win_show(ctgWin, strdup("Categories"),  2, false);
}
void CursesProvider::createPostsMenu(){
    int height, width;
    int n_choices, i = 0;

    const std::vector<PostData> *posts = feedly.givePostsFromStream("All", currentRank);

    if(posts != NULL && posts->size() > 0){
        totalPosts = posts->size();
        numUnread = totalPosts;
        n_choices = posts->size();
        postsItems = (ITEM **)calloc(sizeof(std::vector<PostData>::value_type)*n_choices, sizeof(ITEM *));

        for(auto it = posts->begin(); it != posts->end(); ++it){
            postsItems[i] = new_item((it->title).c_str(), (it->id).c_str());
            i++;
        }

        postsMenu = new_menu((ITEM **)postsItems);
        lastEntryRead = item_description(postsItems[0]);
    }
    else{
        postsMenu = new_menu(NULL);
        currentCategoryRead = true;
        update_panels();
    }

    postsWin = newwin((LINES - 2 - viewWinHeight), 0, 0, ctgWinWidth);
    keypad(postsWin, TRUE);

    getmaxyx(postsWin, height, width);

    set_menu_win(postsMenu, postsWin);
    set_menu_sub(postsMenu, derwin(postsWin, height-4, width-2, 3, 1));
    set_menu_format(postsMenu, height-4, 0);

    set_menu_fore(postsMenu, COLOR_PAIR(7) | A_REVERSE);
    set_menu_back(postsMenu, COLOR_PAIR(6));
    set_menu_grey(postsMenu, COLOR_PAIR(8));

    set_menu_mark(postsMenu, "*");

    win_show(postsWin, strdup("Posts"), 1, true);
    if(posts == NULL){ 
        win_show(ctgWin, strdup("Categories"), 1, true);
        win_show(postsWin, strdup("Posts"), 2, false);
    }

    menu_opts_off(postsMenu, O_SHOWDESC);

    post_menu(postsMenu);

    if(posts == NULL)
        print_in_center(postsWin, 3, 1, height, width-4, "All Posts Read", COLOR_PAIR(0));
}
void CursesProvider::ctgMenuCallback(char* label){
    int startx, starty, height, width;

    getmaxyx(postsWin, height, width);
    getbegyx(postsWin, starty, startx);

    if(activeCount) updateUnreadCount();

    int n_choices, i = 0;
    const std::vector<PostData>* posts = feedly.givePostsFromStream(label, currentRank);
    if(posts == NULL){
        totalPosts = 0;
        numRead = 0;
        numUnread = 0;

        unpost_menu(postsMenu);
        set_menu_items(postsMenu, NULL);
        post_menu(postsMenu);

        print_in_center(postsWin, 3, 1, height, width-4, "All Posts Read", COLOR_PAIR(0));

        currentCategoryRead = true;
        update_statusline("", "", true);

        top = (PANEL *)panel_userptr(top);
        top_panel(top);

        win_show(postsWin, strdup("Posts"), 2, false);
        win_show(ctgWin, strdup("Categories"), 1, true);

        curMenu = ctgMenu;

        return;
    }

    totalPosts = posts->size();
    numRead = 0;
    numUnread = totalPosts;

    n_choices = posts->size() + 1;
    ITEM** items = (ITEM **)calloc(sizeof(std::vector<PostData>::value_type)*n_choices, sizeof(ITEM *));

    for(auto it = posts->begin(); it != posts->end(); ++it){
        items[i] = new_item((it->title).c_str(), (it->id).c_str());
        i++;
    }

    items[i] = NULL;

    unpost_menu(postsMenu);
    set_menu_items(postsMenu, items);
    post_menu(postsMenu);

    set_menu_format(postsMenu, height, 0);
    lastEntryRead = item_description(items[0]);
    currentCategoryRead = false;

    update_statusline("", NULL, true);
    win_show(postsWin, strdup("Posts"), 1, true);
    win_show(ctgWin, strdup("Categories"), 2, false);

    changeSelectedItem(postsMenu, REQ_UP_ITEM);
}
void CursesProvider::changeSelectedItem(MENU* curMenu, int req){
    menu_driver(curMenu, req);
    ITEM* curItem = current_item(curMenu);

    if(curMenu != postsMenu ||  !curItem) return;

    if(activatePreview){
        PostData* data = feedly.getSinglePostData(item_index(curItem));
        std::string PREVIEW_PATH = TMPDIR + "/preview.html";
        std::ofstream myfile (PREVIEW_PATH.c_str());


        if (myfile.is_open())
            myfile << data->content;

        myfile.close();

        FILE* stream = popen(std::string("w3m -dump -cols " + std::to_string(COLS - 2) + " " + PREVIEW_PATH).c_str(), "r");

        std::string content;
        char buffer[256];

        if (stream) {
            while (!feof(stream))
                if (fgets(buffer, 256, stream) != NULL) content.append(buffer);
            pclose(stream);
        }

        wclear(viewWin);
        mvwprintw(viewWin, 1, 1, "%s", content.c_str());
        update_statusline(NULL, std::string(data->originTitle + " - " + data->title).c_str(), true);
    }
    update_panels();
    if(markReadWhileScroll) markItemRead(curItem);
}
void CursesProvider::postsMenuCallback(ITEM* item, bool preview){
    if(access("/usr/bin/w3m", X_OK) != 0) return;
    PostData* container = feedly.getSinglePostData(item_index(item));

    def_prog_mode();
    endwin();
    system(std::string("w3m \'" + container->originURL + "\'").c_str());
    reset_prog_mode();

    markItemRead(item);
    lastEntryRead = item_description(item);
    system(std::string("rm " + TMPDIR + "/preview.html 2> /dev/null").c_str());
}
void CursesProvider::updateUnreadCount(){
    const std::map<std::string, unsigned int>* tmp = feedly.getUnreadCount();

    ITEM* current = current_item(ctgMenu);

    // There is some wierdness with c_str and ncurses so we will hard transform
    // the string. Also ncurses doesn't give a nice function to change an item
    // description so I will just reassign the pointer to it.

    for(int i = 0; i < item_count(ctgMenu);i++){
        if(strncmp(item_name(ctgItems[i]),"Saved",5) == 0)
            continue;
        int num = tmp->at(feedly.getStreamId(item_name(ctgItems[i])));
        std::string count = std::to_string(num);
        char * cstr = new char [count.length()+1];
        std::strcpy (cstr, count.c_str());
        ctgItems[i]->description = TEXT{cstr, (short unsigned int)(count.length() + 1)};
        set_current_item(ctgMenu, ctgItems[i]);
    }

    unpost_menu(ctgMenu);
    set_menu_items(ctgMenu, ctgItems);
    post_menu(ctgMenu);
    set_current_item(ctgMenu, current);

    delete tmp;
}
void CursesProvider::markItemRead(ITEM* item){
    if(item_opts(item) && item != NULL){
        item_opts_off(item, O_SELECTABLE);

        if(item_name(current_item(ctgMenu)) != std::string("Saved")){
            update_statusline("[Marking post read]", NULL, true);

            PostData* container = feedly.getSinglePostData(item_index(item));
            std::vector<std::string> *temp = new std::vector<std::string>;
            temp->push_back(container->id);

            feedly.markPostWithAction("markAsRead", const_cast<std::vector<std::string>*>(temp));
            numUnread--;
            numRead++;
            update_statusline("", NULL, true);
        }

        refresh();
        update_panels();
    }
}
void CursesProvider::newFeedDialog(){
    char feed[200];
    char title[200];
    char ctg[200];
    echo();

    clear_statusline();
    attron(COLOR_PAIR(4));
    mvprintw(LINES - 2, 0, "[ENTER FEED]:");
    mvgetnstr(LINES-2, strlen("[ENTER FEED]") + 1, feed, 200);
    mvaddch(LINES-2, 0, ':');

    clrtoeol();

    mvprintw(LINES - 2, 0, "[ENTER TITLE]:");
    mvgetnstr(LINES-2, strlen("[ENTER TITLE]") + 1, title, 200);
    mvaddch(LINES-2, 0, ':');

    clrtoeol();

    mvprintw(LINES - 2, 0, "[ENTER CATEGORY]:");
    mvgetnstr(LINES-2, strlen("[ENTER CATEGORY]") + 1, ctg, 200);
    mvaddch(LINES-2, 0, ':');

    std::istringstream ss(ctg);
    std::istream_iterator<std::string> begin(ss), end;

    std::vector<std::string> arrayTokens(begin, end);

    noecho();
    clrtoeol();

    update_statusline("[Adding subscription]", NULL, true);
    refresh();

    if(strlen(feed) != 0)
        feedly.addSubscription(false, feed, arrayTokens, title);

    update_statusline("", NULL, true);

}
void CursesProvider::win_show(WINDOW *win, char *label, int label_color, bool highlight){
    int startx, starty, height, width;

    getbegyx(win, starty, startx);
    getmaxyx(win, height, width);

    mvwaddch(win, 2, 0, ACS_LTEE);
    mvwhline(win, 2, 1, ACS_HLINE, width - 2);
    mvwaddch(win, 2, width - 1, ACS_RTEE);

    if(highlight){
        wattron(win, COLOR_PAIR(label_color));
        box(win, 0, 0);
        mvwaddch(win, 2, 0, ACS_LTEE);
        mvwhline(win, 2, 1, ACS_HLINE, width - 2);
        mvwaddch(win, 2, width - 1, ACS_RTEE);
        print_in_middle(win, 1, 0, width, label, COLOR_PAIR(label_color));
        wattroff(win, COLOR_PAIR(label_color));
    }
    else{
        wattron(win, COLOR_PAIR(2));
        box(win, 0, 0);
        mvwaddch(win, 2, 0, ACS_LTEE);
        mvwhline(win, 2, 1, ACS_HLINE, width - 2);
        mvwaddch(win, 2, width - 1, ACS_RTEE);
        print_in_middle(win, 1, 0, width, label, COLOR_PAIR(5));
        wattroff(win, COLOR_PAIR(2));
    }

}
//Print in middle without taking into account height
void CursesProvider::print_in_middle(WINDOW *win, int starty, int startx, int width, const char *str, chtype color){
    int length, x, y;
    float temp;

    if(win == NULL)
        win = stdscr;
    getyx(win, y, x);
    if(startx != 0)
        x = startx;
    if(starty != 0)
        y = starty;
    if(width == 0)
        width = 80;

    length = strlen(str);
    temp = (width - length)/ 2;
    x = startx + (int)temp;
    mvwprintw(win, y, x, "%s", str);
}
//Print in center with height taken into account
void CursesProvider::print_in_center(WINDOW *win, int starty, int startx, int height, int width, const char *str, chtype color){
    int length, x, y;
    float tempX, tempY;

    if(win == NULL)
        win = stdscr;
    getyx(win, y, x);
    if(startx != 0)
        x = startx;
    if(starty != 0)
        y = starty;
    if(width == 0)
        width = 80;

    length = strlen(str);
    tempX = (width - length)/ 2;
    tempY = (height / 2);
    x = startx + (int)tempX;
    y = starty + (int)tempY;
    wattron(win, color);
    mvwprintw(win, y, x, "%s", str);
    wattroff(win, color);
}
void CursesProvider::clear_statusline(){
    move(LINES-2, 0);
    clrtoeol();
}
void CursesProvider::update_statusline(const char* update, const char* post, bool showCounter){
    if (update != NULL)
        statusLine[0] = std::string(update);
    if (post != NULL)
        statusLine[1] = std::string(post);

    if (showCounter) {
        std::stringstream sstm;
        sstm << "[" << numUnread << ":" << numRead << "/" << totalPosts << "]";
        statusLine[2] = sstm.str();
    } else {
        statusLine[2] = std::string();
    }

    clear_statusline();
    move(LINES - 2, 0);
    clrtoeol();

    attron(COLOR_PAIR(1));
    mvprintw(LINES - 2, 0, statusLine[0].c_str());
    attroff(COLOR_PAIR(1));

    mvprintw(LINES - 2, statusLine[0].empty() ? 0 : (statusLine[0].length() + 1), statusLine[1].substr(0,
                COLS - statusLine[0].length() - statusLine[2].length() - 2).c_str());

    attron(COLOR_PAIR(3));
    mvprintw(LINES - 2, COLS - statusLine[2].length(), statusLine[2].c_str());
    attroff(COLOR_PAIR(3));

    refresh();
    update_panels();
}
void CursesProvider::update_infoline(const char* info){
    move(LINES-1, 0);
    clrtoeol();

    attron(COLOR_PAIR(5));
    mvprintw(LINES - 1, 0, info);
    attroff(COLOR_PAIR(5));
}
void CursesProvider::cleanup(){
    if(ctgMenu != NULL){
        unpost_menu(ctgMenu);
        free_menu(ctgMenu);

        for(unsigned int i = 0; i < ARRAY_SIZE(ctgItems); ++i)
            free_item(ctgItems[i]);

        unpost_menu(postsMenu);
        free_menu(postsMenu);
    }

    if(postsItems != NULL){
        for(unsigned int i = 0; i < ARRAY_SIZE(postsItems); ++i)
            free_item(postsItems[i]);
    }

    endwin();
    feedly.curl_cleanup();
}
