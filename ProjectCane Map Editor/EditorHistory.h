#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

template <class Snapshot, class Transform> struct EditorHistoryEntry
{
    enum class Kind
    {
        TransformEdit,
        DocumentEdit
    } kind = Kind::TransformEdit;
    Transform transform;
    Snapshot before;
    Snapshot after;
    std::string label;
};

template <class Snapshot, class Transform> class EditorHistory
{
  public:
    using Entry = EditorHistoryEntry<Snapshot, Transform>;

    void Clear()
    {
        undo_.clear();
        redo_.clear();
        position_ = 0;
        savedPosition_ = 0;
    }
    [[nodiscard]] bool CanUndo() const
    {
        return !undo_.empty();
    }
    [[nodiscard]] bool CanRedo() const
    {
        return !redo_.empty();
    }
    [[nodiscard]] std::size_t UndoCount() const
    {
        return undo_.size();
    }
    [[nodiscard]] std::size_t RedoCount() const
    {
        return redo_.size();
    }
    [[nodiscard]] const std::string &UndoLabel() const
    {
        static const std::string empty;
        return undo_.empty() ? empty : undo_.back().label;
    }
    [[nodiscard]] const std::string &RedoLabel() const
    {
        static const std::string empty;
        return redo_.empty() ? empty : redo_.back().label;
    }
    void MarkSaved()
    {
        savedPosition_ = position_;
    }
    [[nodiscard]] bool IsDirty() const
    {
        return savedPosition_ < 0 || position_ != savedPosition_;
    }

    void Push(Entry entry)
    {
        if (!redo_.empty() && savedPosition_ > position_)
            savedPosition_ = -1;
        undo_.push_back(std::move(entry));
        if (undo_.size() > maximumEntries_)
        {
            undo_.erase(undo_.begin());
            if (savedPosition_ >= 0)
                --savedPosition_;
        }
        redo_.clear();
        ++position_;
    }

    template <class Apply> std::string Undo(Apply &&apply)
    {
        if (!CanUndo())
            return {};
        Entry entry = std::move(undo_.back());
        undo_.pop_back();
        apply(entry, false);
        --position_;
        const std::string label = entry.label;
        redo_.push_back(std::move(entry));
        return label;
    }

    template <class Apply> std::string Redo(Apply &&apply)
    {
        if (!CanRedo())
            return {};
        Entry entry = std::move(redo_.back());
        redo_.pop_back();
        apply(entry, true);
        ++position_;
        const std::string label = entry.label;
        undo_.push_back(std::move(entry));
        return label;
    }

  private:
    std::vector<Entry> undo_;
    std::vector<Entry> redo_;
    std::size_t maximumEntries_ = 128;
    std::int64_t position_ = 0;
    std::int64_t savedPosition_ = 0;
};

template <class Snapshot, class Transform> using HistoryManager = EditorHistory<Snapshot, Transform>;
