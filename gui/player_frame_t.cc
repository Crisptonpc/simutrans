/*
 * Copyright (c) 1997 - 2001 Hansj�rg Malthaner
 *
 * This file is part of the Simutrans project under the artistic licence.
 * (see licence.txt)
 */

/*
 * Player list
 * Hj. Malthaner, 2000
 */

#include <stdio.h>
#include <string.h>

#include "../simcolor.h"
#include "../simworld.h"
#include "../simmenu.h"
#include "../simtool.h"
#include "../network/network_cmd_ingame.h"
#include "../dataobj/environment.h"
#include "../dataobj/scenario.h"
#include "../dataobj/translator.h"

#include "simwin.h"
#include "../utils/simstring.h"
#include "../player/ai_scripted.h"

#include "money_frame.h" // for the finances
#include "password_frame.h" // for the password
#include "ai_selector.h"
#include "player_frame_t.h"


class password_button_t : public button_t
{
public:
	password_button_t() : button_t()
	{
		init(button_t::box, "");
	}

	scr_size get_min_size() const OVERRIDE { return scr_size(D_BUTTON_HEIGHT,D_BUTTON_HEIGHT); }
};



ki_kontroll_t::ki_kontroll_t() :
	gui_frame_t( translator::translate("Spielerliste") )
{
	// switching active player allowed?
	bool player_change_allowed = welt->get_settings().get_allow_player_change() || !welt->get_public_player()->is_locked();

	// activate player etc allowed?
	bool player_tools_allowed = true;

	// check also scenario rules
	if (welt->get_scenario()->is_scripted()) {
		player_tools_allowed = welt->get_scenario()->is_tool_allowed(NULL, TOOL_SWITCH_PLAYER | SIMPLE_TOOL);
		player_change_allowed &= player_tools_allowed;
	}

	set_table_layout(5,0);

	for(int i=0; i<MAX_PLAYER_COUNT-1; i++) {

		const player_t *const player_ = welt->get_player(i);

		// activate player buttons
		// .. not available for the two first players (first human and second public)
		if(  i >= 2  ) {
			// AI button (small square)
			player_active[i-2].init(button_t::square_state, "");
			player_active[i-2].add_listener(this);
			player_active[i-2].set_rigid(true);
			player_active[i-2].set_visible(player_  &&  player_->get_ai_id()!=player_t::HUMAN  &&  player_tools_allowed);
			add_component( player_active+i-2 );
		}
		else {
			new_component<gui_empty_t>();
		}

		// Player select button (arrow)
		player_change_to[i].init(button_t::arrowright_state, "");
		player_change_to[i].add_listener(this);
		player_change_to[i].set_rigid(true);
		add_component(player_change_to+i);

		// Allow player change to human and public only (no AI)
		player_change_to[i].set_visible(player_  &&  player_change_allowed);

		// Prepare finances button
		player_get_finances[i].init( button_t::box | button_t::flexible, "");
		player_get_finances[i].background_color = PLAYER_FLAG | color_idx_to_rgb((player_ ? player_->get_player_color1():i*8)+4);
		player_get_finances[i].add_listener(this);

		// Player type selector, Combobox
		player_select[i].set_focusable( false );

		// Create combobox list data
		player_select[i].new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("slot empty"), SYSCOL_TEXT ) ;
		player_select[i].new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("Manual (Human)"), SYSCOL_TEXT ) ;
		if(  !welt->get_public_player()->is_locked()  ||  !env_t::networkmode  ) {
			player_select[i].new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("Goods AI"), SYSCOL_TEXT ) ;
			player_select[i].new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("Passenger AI"), SYSCOL_TEXT ) ;
			player_select[i].new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("Scripted AI's"), SYSCOL_TEXT ) ;
		}
		assert(  player_t::MAX_AI==5  );

		// add table that contains these two buttons, only one of them will be visible
		add_table(1,0);
		// When adding new players, activate the interface
		player_select[i].set_selection(welt->get_settings().get_player_type(i));
		player_select[i].add_listener(this);

		add_component( player_get_finances+i );
		add_component( player_select+i );
		if(  player_ != NULL  ) {
			player_get_finances[i].set_text( player_->get_name() );
			player_select[i].set_visible(false);
		}
		else {
			player_get_finances[i].set_visible(false);
		}
		end_table();

		// password/locked button
		player_lock[i] = new_component<password_button_t>();
		player_lock[i]->background_color = color_idx_to_rgb((player_ && player_->is_locked()) ? (player_->is_unlock_pending() ? COL_YELLOW : COL_RED) : COL_GREEN);
		player_lock[i]->enable( welt->get_player(i) );
		player_lock[i]->add_listener(this);
		player_lock[i]->set_rigid(true);
		player_lock[i]->set_visible( player_tools_allowed );

		// Income label
		ai_income[i] = new_component<gui_label_buf_t>(MONEY_PLUS, gui_label_t::money_right);
		ai_income[i]->set_rigid(true);
	}

	// freeplay mode
	freeplay.init( button_t::square_state, "freeplay mode");
	freeplay.add_listener(this);
	if (welt->get_public_player()->is_locked() || !welt->get_settings().get_allow_player_change()  ||  !player_tools_allowed) {
		freeplay.disable();
	}
	freeplay.pressed = welt->get_settings().is_freeplay();
	add_component( &freeplay, 5 );

	update_income();
	update_data(); // calls reset_min_windowsize

	set_windowsize(get_min_windowsize());
}


/**
 * This method is called if an action is triggered
 * @author Hj. Malthaner
 */
bool ki_kontroll_t::action_triggered( gui_action_creator_t *comp,value_t p )
{
	static char param[16];

	// Free play button?
	if(  comp == &freeplay  ) {
		welt->call_change_player_tool(karte_t::toggle_freeplay, 255, 0);
		return true;
	}

	// Check the GUI list of buttons
	for(int i=0; i<MAX_PLAYER_COUNT-1; i++) {
		if(  i>=2  &&  comp == (player_active+i-2)  ) {
			// switch AI on/off
			if(  welt->get_player(i)==NULL  ) {
				// create new AI
				welt->call_change_player_tool(karte_t::new_player, i, player_select[i].get_selection());
				player_lock[i]->enable( welt->get_player(i) );

				// if scripted ai without script -> open script selector window
				ai_scripted_t *ai = dynamic_cast<ai_scripted_t*>(welt->get_player(i));
				if (ai  &&  !ai->has_script()) {
					create_win( new ai_selector_t(i), w_info, magic_finances_t + i );
				}
			}
			else {
				// If turning on again, reload script
				if (!env_t::networkmode  &&  !welt->get_player(i)->is_active()) {
					if (ai_scripted_t *ai = dynamic_cast<ai_scripted_t*>(welt->get_player(i))) {
						ai->reload_script();
					}
				}
				// Current AI on/off
				sprintf( param, "a,%i,%i", i, !welt->get_player(i)->is_active() );
				tool_t::simple_tool[TOOL_CHANGE_PLAYER]->set_default_param( param );
				welt->set_tool( tool_t::simple_tool[TOOL_CHANGE_PLAYER], welt->get_active_player() );
			}
			break;
		}

		// Finance button pressed
		if(  comp == (player_get_finances+i)  ) {
			// get finances
			player_get_finances[i].pressed = false;
			// if scripted ai without script -> open script selector window
			ai_scripted_t *ai = dynamic_cast<ai_scripted_t*>(welt->get_player(i));
			if (ai  &&  !ai->has_script()) {
				create_win( new ai_selector_t(i), w_info, magic_finances_t + i );
			}
			else {
				create_win( new money_frame_t(welt->get_player(i)), w_info, magic_finances_t + i );
			}
			break;
		}

		// Changed active player
		if(  comp == (player_change_to+i)  ) {
			// make active player
			player_t *const prevplayer = welt->get_active_player();
			welt->switch_active_player(i,false);

			// unlocked public service player can change into any company in multiplayer games
			player_t *const player = welt->get_active_player();
			if(  env_t::networkmode  &&  prevplayer == welt->get_public_player()  &&  !prevplayer->is_locked()  &&  player->is_locked()  ) {
				player->unlock(false, true);

				// send unlock command
				nwc_auth_player_t *nwc = new nwc_auth_player_t();
				nwc->player_nr = player->get_player_nr();
				network_send_server(nwc);
			}

			break;
		}

		// Change player name and/or password
		if(  comp == (player_lock[i])  &&  welt->get_player(i)  ) {
			if (!welt->get_player(i)->is_unlock_pending()) {
				// set password
				create_win( -1, -1, new password_frame_t(welt->get_player(i)), w_info, magic_pwd_t + i );
				player_lock[i]->pressed = false;
			}
		}

		// New player assigned in an empty slot
		if(  comp == (player_select+i)  ) {

			// make active player
			if(  p.i<player_t::MAX_AI  &&  p.i>0  ) {
				player_active[i-2].set_visible(true);
				welt->get_settings().set_player_type(i, (uint8)p.i);
			}
			else {
				player_active[i-2].set_visible(false);
				player_select[i].set_selection(0);
				welt->get_settings().set_player_type(i, 0);
			}
			break;
		}

	}
	return true;
}


void ki_kontroll_t::update_data()
{
	for(int i=0; i<MAX_PLAYER_COUNT-1; i++) {

		if(  player_t *player = welt->get_player(i)  ) {

			// active player -> remove selection
			if (player_select[i].is_visible()) {
				player_select[i].set_visible(false);
				player_get_finances[i].set_visible(true);
				player_change_to[i].set_visible(true);
			}

			// scripted ai without script get different button without color
			ai_scripted_t *ai = dynamic_cast<ai_scripted_t*>(player);

			if (ai  &&  !ai->has_script()) {
				player_get_finances[i].set_typ(button_t::roundbox | button_t::flexible);
				player_get_finances[i].set_text("Load scripted AI");
			}
			else {
				player_get_finances[i].set_typ(button_t::box | button_t::flexible);
				player_get_finances[i].set_text(player->get_name());
			}

			// always update locking status
			player_get_finances[i].background_color = PLAYER_FLAG | color_idx_to_rgb(player->get_player_color1()+4);
			player_lock[i]->background_color = color_idx_to_rgb(player->is_locked() ? (player->is_unlock_pending() ? COL_YELLOW : COL_RED) : COL_GREEN);

			// human players cannot be deactivated
			if (i>1) {
				player_active[i-2].set_visible( player->get_ai_id()!=player_t::HUMAN );
			}

			ai_income[i]->set_visible(true);
		}
		else {

			// inactive player => button needs removal?
			if (player_get_finances[i].is_visible()) {
				player_get_finances[i].set_visible(false);
				player_change_to[i].set_visible(false);
				player_select[i].set_visible(true);
			}

			if (i>1) {
				player_active[i-2].set_visible(0 < player_select[i].get_selection()  &&  player_select[i].get_selection() < player_t::MAX_AI);
			}

			if(  env_t::networkmode  ) {

				// change available selection of AIs
				if(  !welt->get_public_player()->is_locked()  ) {
					if(  player_select[i].count_elements()==2  ) {
						player_select[i].new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("Goods AI"), SYSCOL_TEXT ) ;
						player_select[i].new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("Passenger AI"), SYSCOL_TEXT ) ;
					}
				}
				else {
					if(  player_select[i].count_elements()==4  ) {
						player_select[i].clear_elements();
						player_select[i].new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("slot empty"), SYSCOL_TEXT ) ;
						player_select[i].new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("Manual (Human)"), SYSCOL_TEXT ) ;
					}
				}

			}
			ai_income[i]->set_visible(false);
		}
		assert( player_select[i].is_visible() ^  player_get_finances[i].is_visible() );
	}
	reset_min_windowsize();
}


void ki_kontroll_t::update_income()
{
	// Update finance
	for(int i=0; i<MAX_PLAYER_COUNT-1; i++) {
		ai_income[i]->buf().clear();
		player_t *player = welt->get_player(i);
		if(  player != NULL  ) {
			if (i != 1 && !welt->get_settings().is_freeplay() && player->get_finance()->get_history_com_year(0, ATC_NETWEALTH) < 0) {
				ai_income[i]->set_color( MONEY_MINUS );
				ai_income[i]->buf().append(translator::translate("Company bankrupt"));
			}
			else {
				double account=player->get_account_balance_as_double();
				char str[128];
				money_to_string(str, account );
				ai_income[i]->buf().append(str);
				ai_income[i]->set_color( account>=0.0 ? MONEY_PLUS : MONEY_MINUS );
			}
		}
		ai_income[i]->update();
	}
}

/**
 * Draw the component
 * @author Hj. Malthaner
 */
void ki_kontroll_t::draw(scr_coord pos, scr_size size)
{
	// Update free play
	freeplay.pressed = welt->get_settings().is_freeplay();
	if (welt->get_public_player()->is_locked() || !welt->get_settings().get_allow_player_change()) {
		freeplay.disable();
	}
	else {
		freeplay.enable();
	}

	// Update finance
	update_income();

	// Update buttons
	for(int i=0; i<MAX_PLAYER_COUNT-1; i++) {

		player_t *player = welt->get_player(i);

		player_change_to[i].pressed = false;
		if(i>=2) {
			player_active[i-2].pressed = player !=NULL  &&  player->is_active();
		}

		player_lock[i]->background_color = color_idx_to_rgb(player  &&  player->is_locked() ? (player->is_unlock_pending() ? COL_YELLOW : COL_RED) : COL_GREEN);
	}

	player_change_to[welt->get_active_player_nr()].pressed = true;

	// All controls updated, draw them...
	gui_frame_t::draw(pos, size);
}
