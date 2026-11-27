#ifndef __UI_VIEW_EMPTY_STATE_VIEW_HPP__
#define __UI_VIEW_EMPTY_STATE_VIEW_HPP__

#include <string>

#include "ui/view/icon_view.hpp"
#include "ui/view/text_view.hpp"
#include "ui/view/ui_layout.hpp"

/**
 * The empty state of a screen with no item: the empty icon, a message, and a line that tells the
 * user what to do, in the middle of the space that the parent gives.
 */
class EmptyStateView : public UiLayout {
public:
    EmptyStateView();

    void setMessage(const std::string& text) { message.setText(text); }
    void setLine(const std::string& text) { line.setText(text); }

    void layout(UiLayoutContext& context) override;

protected:
    void onAttach(UiContext& context) override;

private:
    IconView icon{ICON_EMPTY};
    TextView message;
    TextView line;
};

#endif
