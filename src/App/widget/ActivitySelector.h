#include <Modal.h>
#include <storage/Storage.h>
#include <storage/Types.h>

namespace ST {

struct ActivitySelector : public ListBox<ActivityID> {
    using Submit = std::function<void(std::shared_ptr<ActivitySelector> const &, ActivityID const &)>;
    Submit   submit_fnc;
    Month    month;
    Storage &storage;

    ActivitySelector(Submit const &submit, Month const &month, Storage &storage);
    void populate();
    void submit() override;
    bool process_key(KeyboardModifier modifier, int key) override;
};

}
