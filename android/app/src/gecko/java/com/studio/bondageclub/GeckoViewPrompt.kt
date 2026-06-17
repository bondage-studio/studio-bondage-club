package com.studio.bondageclub

import android.app.Activity
import android.app.AlertDialog
import android.widget.EditText
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoSession.PromptDelegate
import org.mozilla.geckoview.GeckoSession.PromptDelegate.AlertPrompt
import org.mozilla.geckoview.GeckoSession.PromptDelegate.BasePrompt
import org.mozilla.geckoview.GeckoSession.PromptDelegate.ButtonPrompt
import org.mozilla.geckoview.GeckoSession.PromptDelegate.ChoicePrompt
import org.mozilla.geckoview.GeckoSession.PromptDelegate.PromptInstanceDelegate
import org.mozilla.geckoview.GeckoSession.PromptDelegate.PromptResponse
import org.mozilla.geckoview.GeckoSession.PromptDelegate.TextPrompt

// GeckoView ships no UI for HTML form/window prompts — unlike the Android System
// WebView (the `system` flavor), the embedder must supply it via a PromptDelegate.
// Without this, tapping an HTML <select> does nothing (no dropdown opens), and
// window.alert/confirm/prompt silently no-op. This delegate renders those with
// plain AlertDialogs so the game's native dropdowns work in the gecko flavor.
class GeckoViewPrompt(private val activity: Activity) : PromptDelegate {

    // Confirms a prompt exactly once. GeckoResult.complete and BasePrompt.confirm
    // both throw if called twice, and dialog button + cancel callbacks can race,
    // so funnel every completion through this guard.
    private class OneShot(private val result: GeckoResult<PromptResponse>) {
        private var done = false
        fun complete(response: PromptResponse) {
            if (done) return
            done = true
            result.complete(response)
        }
    }

    override fun onChoicePrompt(
        session: GeckoSession,
        prompt: ChoicePrompt,
    ): GeckoResult<PromptResponse> {
        val result = GeckoResult<PromptResponse>()
        val once = OneShot(result)

        // Flatten <optgroup>s to their leaf <option>s; a single-choice list can't
        // represent group headers, and the game's selects don't use them.
        val choices = ArrayList<ChoicePrompt.Choice>()
        flatten(prompt.choices, choices)
        val labels = choices.map { it.label }.toTypedArray()

        val builder = AlertDialog.Builder(activity)
        if (!prompt.title.isNullOrEmpty()) builder.setTitle(prompt.title)

        if (prompt.type == ChoicePrompt.Type.MULTIPLE) {
            val checked = BooleanArray(choices.size) { choices[it].selected }
            builder.setMultiChoiceItems(labels, checked) { _, which, isChecked ->
                checked[which] = isChecked
            }
            builder.setPositiveButton(android.R.string.ok) { _, _ ->
                val ids = choices.filterIndexed { i, _ -> checked[i] }.map { it.id }
                once.complete(prompt.confirm(ids.toTypedArray()))
            }
            builder.setNegativeButton(android.R.string.cancel) { _, _ ->
                once.complete(prompt.dismiss())
            }
        } else {
            // SINGLE (a plain <select>) and MENU: tapping a row confirms at once.
            val selected = choices.indexOfFirst { it.selected }
            builder.setSingleChoiceItems(labels, selected) { dialog, which ->
                once.complete(prompt.confirm(choices[which].id))
                dialog.dismiss()
            }
            builder.setNegativeButton(android.R.string.cancel) { _, _ ->
                once.complete(prompt.dismiss())
            }
        }

        showDialog(builder, prompt, once)
        return result
    }

    override fun onAlertPrompt(
        session: GeckoSession,
        prompt: AlertPrompt,
    ): GeckoResult<PromptResponse> {
        val result = GeckoResult<PromptResponse>()
        val once = OneShot(result)
        val builder = AlertDialog.Builder(activity)
            .setTitle(prompt.title)
            .setMessage(prompt.message)
            .setPositiveButton(android.R.string.ok) { _, _ -> once.complete(prompt.dismiss()) }
        showDialog(builder, prompt, once)
        return result
    }

    override fun onButtonPrompt(
        session: GeckoSession,
        prompt: ButtonPrompt,
    ): GeckoResult<PromptResponse> {
        val result = GeckoResult<PromptResponse>()
        val once = OneShot(result)
        val builder = AlertDialog.Builder(activity)
            .setTitle(prompt.title)
            .setMessage(prompt.message)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                once.complete(prompt.confirm(ButtonPrompt.Type.POSITIVE))
            }
            .setNegativeButton(android.R.string.cancel) { _, _ ->
                once.complete(prompt.confirm(ButtonPrompt.Type.NEGATIVE))
            }
        showDialog(builder, prompt, once)
        return result
    }

    override fun onTextPrompt(
        session: GeckoSession,
        prompt: TextPrompt,
    ): GeckoResult<PromptResponse> {
        val result = GeckoResult<PromptResponse>()
        val once = OneShot(result)
        val input = EditText(activity).apply { setText(prompt.defaultValue) }
        val builder = AlertDialog.Builder(activity)
            .setTitle(prompt.title)
            .setMessage(prompt.message)
            .setView(input)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                once.complete(prompt.confirm(input.text.toString()))
            }
            .setNegativeButton(android.R.string.cancel) { _, _ -> once.complete(prompt.dismiss()) }
        showDialog(builder, prompt, once)
        return result
    }

    // Wires the shared teardown paths: a user cancel (back press / outside tap)
    // dismisses the prompt, and an engine-side dismissal closes the dialog.
    private fun showDialog(builder: AlertDialog.Builder, prompt: BasePrompt, once: OneShot) {
        val dialog = builder.create()
        dialog.setOnCancelListener { once.complete(prompt.dismiss()) }
        prompt.setDelegate(object : PromptInstanceDelegate {
            override fun onPromptDismiss(closing: BasePrompt) {
                dialog.dismiss()
            }
        })
        dialog.show()
    }

    private fun flatten(choices: Array<ChoicePrompt.Choice>, out: MutableList<ChoicePrompt.Choice>) {
        for (choice in choices) {
            val items = choice.items
            if (items != null && items.isNotEmpty()) {
                flatten(items, out)
            } else {
                out.add(choice)
            }
        }
    }
}